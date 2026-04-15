#!/usr/bin/env python3
"""
check_fb_calls.py — FB layer forbidden function call and header checker.

Enforces the following rules across fbs/drivers/, fbs/services/, core/src/:
  I-07   — No malloc/free/calloc/realloc in Layer 1 or Layer 2
  R-02   — No dynamic allocation in Layer 1 or Core
  R-sub-03  — No direct OSAL, RTOS, or POSIX calls from FB code
  R-sub-03a — No blocking calls (sleep/delay) in dispatch threads

Exits 0 on clean scan. Exits 1 on any violation.

Usage:
  python3 tools/ci/check_fb_calls.py [--strict]

The --strict flag is always implied for CI. The script exits non-zero on
any violation regardless of flags.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import re
import sys
from pathlib import Path

# ── Allowlisted paths (framework internals — exempt from FB rules) ───────
# These files implement the framework itself ON TOP of the OSAL layer.
# They are permitted to call OSAL primitives directly by design.
# Application FBs written by contributors must never appear here.
#
# core/src/bus/       — message bus implementation (IS the OSAL consumer)
# core/src/registry/  — FB engine (creates dispatch tasks via OSAL)
# fb_timer.c          — Platform FB: OSAL timer bridge (R-sub-11)
# fb_watchdog.c       — Platform FB: OSAL watchdog bridge (R-sub-11)
# fb_nvm.c            — Platform FB: OSAL NVM bridge (R-sub-11)
#
# To add a new allowlisted file: get explicit PM approval and document why.
ALLOWLIST_PATHS = [
    'core/src/bus/',
    'core/src/registry/',
    'fbs/drivers/fb_timer.c',
    'fbs/drivers/fb_watchdog.c',
    'fbs/drivers/fb_nvm.c',
]

# ── Directories to scan (relative to repo root) ─────────────────────────
SCAN_DIRS = [
    'fbs/drivers',
    'fbs/services',
    'core/src',
]

# ── Forbidden function call patterns ────────────────────────────────────
# Format: (regex_pattern, rule_id, human_message)
FORBIDDEN_CALLS = [
    # Dynamic allocation — I-07, R-02
    (r'\b(malloc|free|calloc|realloc)\s*\(',
     'I-07/R-02', 'dynamic allocation forbidden in Layer 1/2 — use static arrays'),

    # Blocking calls — R-sub-03a
    (r'\b(usleep|sleep|nanosleep)\s*\(',
     'R-sub-03a', 'blocking sleep call in dispatch thread — never block in a handler'),
    (r'\bembediq_osal_delay_ms\s*\(',
     'R-sub-03a', 'blocking delay in dispatch thread — never block in a handler'),

    # Direct FreeRTOS calls — R-sub-03
    (r'\b(xTaskCreate|xTaskCreateStatic|vTaskDelete|vTaskDelay|vTaskDelayUntil'
     r'|vTaskSuspend|vTaskResume)\s*\(',
     'R-sub-03', 'direct FreeRTOS task call — framework owns all task lifecycle'),
    (r'\b(xSemaphoreTake|xSemaphoreGive|xSemaphoreGiveFromISR'
     r'|xSemaphoreCreateMutex|xSemaphoreCreateBinary'
     r'|xSemaphoreCreateCounting)\s*\(',
     'R-sub-03', 'direct FreeRTOS semaphore call — use OSAL abstraction'),
    (r'\b(xQueueCreate|xQueueCreateStatic|xQueueSend|xQueueSendFromISR'
     r'|xQueueReceive|xQueuePeek)\s*\(',
     'R-sub-03', 'direct FreeRTOS queue call — use message bus API'),

    # Direct POSIX threading — R-sub-03
    (r'\b(pthread_create|pthread_join|pthread_cancel|pthread_detach)\s*\(',
     'R-sub-03', 'direct POSIX thread call — framework owns all thread lifecycle'),
    (r'\b(pthread_mutex_lock|pthread_mutex_trylock|pthread_mutex_unlock'
     r'|pthread_mutex_init|pthread_mutex_destroy)\s*\(',
     'R-sub-03', 'direct POSIX mutex call — use OSAL abstraction'),
    (r'\b(pthread_cond_wait|pthread_cond_timedwait'
     r'|pthread_cond_signal|pthread_cond_broadcast)\s*\(',
     'R-sub-03', 'direct POSIX condition variable — use OSAL abstraction'),
    (r'\bsem_wait\s*\(|sem_post\s*\(|sem_init\s*\(|sem_destroy\s*\(',
     'R-sub-03', 'direct POSIX semaphore call — use OSAL abstraction'),

    # Direct OSAL calls from FB code — R-sub-03
    (r'\bembediq_osal_task_create\s*\(',
     'R-sub-03', 'FB code must not create tasks — framework owns task lifecycle'),
    (r'\bembediq_osal_task_delete\s*\(',
     'R-sub-03', 'FB code must not delete tasks — framework owns task lifecycle'),
    (r'\bembediq_osal_mutex_(lock|unlock|create|destroy)\s*\(',
     'R-sub-03', 'FB code must not call OSAL mutex directly — use message bus'),
    (r'\bembediq_osal_queue_(create|send|recv|destroy|flush)\s*\(',
     'R-sub-03', 'FB code must not call OSAL queue directly — use message bus'),
    (r'\bembediq_osal_signal_(create|post|wait|destroy)\s*\(',
     'R-sub-03', 'FB code must not call OSAL signal directly — use message bus'),
    (r'\bembediq_sem_(create|post|wait|destroy|post_from_isr)\s*\(',
     'R-sub-03', 'FB code must not call OSAL semaphore directly — use message bus'),
]

# ── Forbidden system headers in FB layers ───────────────────────────────
# These are angle-bracket system headers that are platform-specific and
# violate layer isolation when included in fbs/ code.
FORBIDDEN_HEADERS = [
    (r'#\s*include\s*<unistd\.h>',
     'R-sub-03a', '<unistd.h> in fbs/ — use embediq_osal_time_ms() and embediq_osal_delay_ms()'),
    (r'#\s*include\s*<pthread\.h>',
     'R-sub-03', '<pthread.h> in fbs/ — direct POSIX threading forbidden in FB code'),
    (r'#\s*include\s*<semaphore\.h>',
     'R-sub-03', '<semaphore.h> in fbs/ — use OSAL abstraction'),
    (r'#\s*include\s*<sys/socket\.h>',
     'R-sub-03', '<sys/socket.h> in fbs/ — use ops table abstraction for networking'),
    (r'#\s*include\s*<sys/time\.h>',
     'R-sub-03', '<sys/time.h> in fbs/ — use embediq_osal_time_us() or embediq_osal_time_ms()'),
]


def is_allowlisted(path: Path, repo_root: Path) -> bool:
    rel = str(path.relative_to(repo_root)).replace('\\', '/')
    return any(rel.startswith(p) or rel.endswith(p.lstrip('/'))
               for p in ALLOWLIST_PATHS)


def is_comment_line(line: str) -> bool:
    """Return True if line is purely a comment (skip to avoid false positives)."""
    s = line.strip()
    return s.startswith('//') or s.startswith('*') or s.startswith('/*')


def scan_file(path: Path) -> list:
    violations = []
    try:
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    except Exception as exc:
        return [f'{path}:0: [ERROR] Cannot read file: {exc}']

    for lineno, line in enumerate(lines, 1):
        if is_comment_line(line):
            continue
        for pattern, rule, message in FORBIDDEN_CALLS:
            if re.search(pattern, line):
                violations.append(
                    f'{path}:{lineno}: [{rule}] {message}\n'
                    f'    {line.strip()}'
                )
        for pattern, rule, message in FORBIDDEN_HEADERS:
            if re.search(pattern, line):
                violations.append(
                    f'{path}:{lineno}: [{rule}] {message}\n'
                    f'    {line.strip()}'
                )
    return violations


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent.parent
    all_violations = []
    files_scanned = 0

    for scan_dir in SCAN_DIRS:
        dir_path = repo_root / scan_dir
        if not dir_path.exists():
            continue
        for src_file in sorted(dir_path.rglob('*.[ch]')):
            if is_allowlisted(src_file, repo_root):
                continue
            files_scanned += 1
            all_violations.extend(scan_file(src_file))

    if all_violations:
        print(
            f'\ncheck_fb_calls.py: {len(all_violations)} violation(s) in '
            f'{files_scanned} file(s) scanned:\n'
        )
        for v in all_violations:
            print(f'  {v}')
        print(
            f'\nAll violations must be fixed before merging.\n'
            f'See CODING_RULES.md: I-07, R-02, R-sub-03, R-sub-03a.'
        )
        return 1

    print(f'check_fb_calls.py: OK — {files_scanned} file(s) scanned, 0 violations.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
