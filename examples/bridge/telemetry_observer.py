#!/usr/bin/env python3
# examples/bridge/telemetry_observer.py — Live telemetry table demo
#
# A Python External FB that connects to fb_bridge via the embediq-python SDK,
# subscribes to MSG_TELEMETRY_GAUGE (0x0640) and MSG_TELEMETRY_COUNTER (0x0641),
# and renders an in-place terminal table that updates as messages arrive.
# Stdlib-only beyond the embediq-python SDK.
#
# Usage:
#   pip install -e tools/embediq_python      # one-time install
#   EMBEDIQ_BRIDGE_SOCK=/tmp/embediq_bridge.sock \
#     python3 examples/bridge/telemetry_observer.py
#
# Ctrl+C exits cleanly.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import signal
import sys
import threading
from datetime import datetime

from embediq import ExternalFB
from embediq._msg import register_msg_registry
from embediq.msgs.telemetry_msgs import (
    MSG_TELEMETRY_GAUGE, MSG_TELEMETRY_COUNTER,
    _MSG_REGISTRY as TEL_REGISTRY,
)

# Register the telemetry payload decoders so msg.decode() returns typed objects.
register_msg_registry(TEL_REGISTRY)

UNIT_LABELS = {0: '', 1: '°C', 2: '%', 3: 'ms', 4: 'mA', 5: 'mV', 255: '?'}
_CLEAR_LINE = '\033[2K\r'   # erase current line, return cursor to column 0
_UP         = '\033[1A'     # move cursor up one line


class TelemetryObserver(ExternalFB):
    name = 'telemetry_observer'

    def __init__(self):
        super().__init__()
        self._lock = threading.Lock()
        self._rows = {}             # (kind, metric_id) -> row dict
        self._last_lines = 0
        self._last_update = None

    def on_connect(self):
        self.subscribe(MSG_TELEMETRY_GAUGE, MSG_TELEMETRY_COUNTER)

    def on_message(self, msg):
        p = msg.decode()
        with self._lock:
            if msg.msg_id == MSG_TELEMETRY_GAUGE:
                row = self._rows.setdefault(('G', p.metric_id),
                    {'kind': 'G', 'metric': p.metric_id, 'value': 0.0,
                     'unit': p.unit_id, 'count': 0})
                row['value'] = float(p.value)
            elif msg.msg_id == MSG_TELEMETRY_COUNTER:
                row = self._rows.setdefault(('C', p.metric_id),
                    {'kind': 'C', 'metric': p.metric_id, 'value': 0,
                     'unit': p.unit_id, 'count': 0})
                row['value'] += int(p.delta)
            else:
                return
            row['unit'] = p.unit_id
            row['count'] += 1
            self._last_update = datetime.now()
        self._redraw()

    def on_disconnect(self):
        sys.stderr.write('\n(connection lost — auto-reconnecting…)\n')

    def _redraw(self):
        with self._lock:
            rows = sorted(self._rows.values(), key=lambda r: (r['kind'], r['metric']))
            stamp = self._last_update.strftime('%H:%M:%S') if self._last_update else '—'

        lines = [
            f"{'TYPE':<8}{'METRIC':<14}{'VALUE':>10}   {'UNIT':<6}{'COUNT':>8}",
            '─' * 54,
        ]
        if not rows:
            lines.append('(no data yet — waiting for fb_telemetry...)')
        else:
            for r in rows:
                metric = f"0x{r['metric']:04X}"
                val = (f"{r['value']:>10.3f}" if r['kind'] == 'G'
                       else f"{int(r['value']):>10d}")
                unit = UNIT_LABELS.get(r['unit'], '?')
                lines.append(f"{r['kind']:<8}{metric:<14}{val}   {unit:<6}{r['count']:>8}")
        lines.append(f'  updated {stamp}')

        if self._last_lines > 0:
            sys.stdout.write(_UP * self._last_lines)
        for line in lines:
            sys.stdout.write(_CLEAR_LINE + line + '\n')
        # If the table shrank, clear leftover rows from the prior render.
        if self._last_lines > len(lines):
            extra = self._last_lines - len(lines)
            for _ in range(extra):
                sys.stdout.write(_CLEAR_LINE + '\n')
            sys.stdout.write(_UP * extra)
        sys.stdout.flush()
        self._last_lines = len(lines)


def main():
    observer = TelemetryObserver()

    def _on_sigint(signum, frame):
        observer.disconnect()
        sys.stdout.write('\n')
        sys.exit(0)

    signal.signal(signal.SIGINT, _on_sigint)
    observer.connect()
    observer.run()


if __name__ == '__main__':
    main()
