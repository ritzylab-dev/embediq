#define _POSIX_C_SOURCE 200809L   /* setenv, unsetenv, fork, execv */

/*
 * tests/unit/test_cfg_store_isolation.c — the configuration test never uses
 * an inherited store
 *
 * Runs the real configuration test binary (path in argv[1]) once per case,
 * with EMBEDIQ_NVM_PATH inherited from this process, and checks that the
 * inherited store is untouched and that the binary used a private store:
 *
 *   existing     a valid store holding a sentinel key → the binary passes its
 *                18 checks; the store is byte-identical and the sentinel
 *                reloads; the binary's own store was elsewhere and is gone
 *   fresh        a path with no file → passes; no file and no ".tmp" appear
 *   unreadable   a mode-000 copy of the store → passes; mode and bytes are
 *                unchanged (skipped when running as root, which reads it)
 *   setup fails  TMPDIR names a missing directory → the binary exits non-zero
 *                before any check runs; the inherited store is byte-identical
 *
 * This process selects its own private store first; the inherited stores are
 * extra files inside that private directory, so nothing here touches a store
 * outside it.
 *
 * Run:      ./build/tests/unit/test_cfg_store_isolation ./build/tests/unit/test_embediq_cfg
 * Expected: "All N checks passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_nvm.h"
#include "embediq_obs.h"
#include "embediq_test_store.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern void nvm__init_state(void);
extern void nvm__load_state(void);

#define SENTINEL_KEY  "test.sentinel"
#define SENTINEL_VAL  "keep"

/* The configuration test's pass line: its 18 checks, unchanged by this test. */
#define CFG_ALL_PASSED  "All 18 tests passed. (0 failed)"

/* Captured child output: the 18 PASS lines and the store line are well under
 * 4 KiB; 64 KiB leaves room for diagnostics without growing a buffer. */
#define CHILD_OUTPUT_MAX  (64u * 1024u)

/* The flash HAL refuses a store larger than 64 KiB (HAL_FLASH_POSIX_MAX_SIZE),
 * so a valid store fits; one more byte detects a larger file. */
#define STORE_MAX  (64u * 1024u + 1u)

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)  do {                                                  \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %s  %s:%d\n", (msg), __FILE__, __LINE__);        \
        g_failed++;                                                             \
    } else {                                                                    \
        printf("PASS  %s\n", (msg));                                            \
    }                                                                           \
} while (0)

static char   g_out[CHILD_OUTPUT_MAX + 1u];
static size_t g_out_len;

/* Run argv0 once with this process's environment; capture stdout and stderr
 * into g_out. Returns the exit status, or -1 if it did not exit normally. */
static int run_child(const char *argv0)
{
    int fds[2];
    if (pipe(fds) != 0) return -1;
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)dup2(fds[1], STDOUT_FILENO);
        (void)dup2(fds[1], STDERR_FILENO);
        (void)close(fds[0]);
        (void)close(fds[1]);
        char *const args[] = { (char *)argv0, NULL };
        execv(argv0, args);
        _exit(127);
    }
    (void)close(fds[1]);
    g_out_len = 0u;
    ssize_t n;
    while ((n = read(fds[0], g_out + g_out_len, CHILD_OUTPUT_MAX - g_out_len)) > 0) {
        g_out_len += (size_t)n;
        if (g_out_len == CHILD_OUTPUT_MAX) break;
    }
    g_out[g_out_len] = '\0';
    (void)close(fds[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

/* The private store the child announced ("test store: <path>"), or "". */
static void child_store(char *out, size_t size)
{
    out[0] = '\0';
    const char *p = strstr(g_out, "test store: ");
    if (!p) return;
    p += strlen("test store: ");
    size_t len = strcspn(p, "\n");
    if (len >= size) return;
    memcpy(out, p, len);
    out[len] = '\0';
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read the whole file; returns its length, or -1. */
static long read_all(const char *path, unsigned char *buf)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1u, STORE_MAX, f);
    bool err = ferror(f) != 0;
    (void)fclose(f);
    return err ? -1 : (long)n;
}

static bool write_all(const char *path, const unsigned char *buf, long len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1u, (size_t)len, f) == (size_t)len;
    return (fclose(f) == 0) && ok;
}

static bool sentinel_reloads(const char *path)
{
    setenv("EMBEDIQ_NVM_PATH", path, 1);
    nvm__init_state();
    nvm__load_state();
    char     val[sizeof(SENTINEL_VAL)] = {0};
    uint32_t len = (uint32_t)sizeof(val);
    return embediq_nvm_get(SENTINEL_KEY, val, &len) == EMBEDIQ_OK
        && len == (uint32_t)strlen(SENTINEL_VAL)
        && memcmp(val, SENTINEL_VAL, len) == 0;
}

/* The child passed, used a store other than inherited, and removed it. */
static void check_private_run(const char *name, int rc, const char *inherited)
{
    char store[PATH_MAX];
    char msg[PATH_MAX];
    child_store(store, sizeof(store));
    snprintf(msg, sizeof(msg), "%s: the configuration test passes its 18 checks", name);
    CHECK(rc == 0 && strstr(g_out, CFG_ALL_PASSED) != NULL, msg);
    snprintf(msg, sizeof(msg), "%s: it announced a private store", name);
    CHECK(store[0] != '\0' && strcmp(store, inherited) != 0, msg);
    snprintf(msg, sizeof(msg), "%s: its private store was removed at exit", name);
    CHECK(store[0] != '\0' && !exists(store), msg);
}

static unsigned char g_seed[STORE_MAX];
static unsigned char g_after[STORE_MAX];

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test_embediq_cfg binary>\n", argv[0]);
        return 2;
    }
    const char *cfg_bin = argv[1];
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    (void)embediq_test_store_select();   /* this process's own store first */
    const char *existing   = embediq_test_store_file("inherited_existing.bin");
    const char *fresh      = embediq_test_store_file("inherited_fresh.bin");
    const char *unreadable = embediq_test_store_file("inherited_unreadable.bin");

    /* A valid store with the sentinel key, written through the real NVM path. */
    setenv("EMBEDIQ_NVM_PATH", existing, 1);
    nvm__init_state();
    if (embediq_nvm_set(SENTINEL_KEY, SENTINEL_VAL, (uint32_t)strlen(SENTINEL_VAL))
            != EMBEDIQ_OK) {
        fprintf(stderr, "cannot seed the inherited store\n");
        return 2;
    }
    long seed_len = read_all(existing, g_seed);
    if (seed_len <= 0 || !sentinel_reloads(existing)) {
        fprintf(stderr, "seeded store unreadable or sentinel missing\n");
        return 2;
    }

    /* --- existing --- */
    setenv("EMBEDIQ_NVM_PATH", existing, 1);
    int rc = run_child(cfg_bin);
    check_private_run("existing", rc, existing);
    long len = read_all(existing, g_after);
    CHECK(len == seed_len && memcmp(g_seed, g_after, (size_t)seed_len) == 0,
          "existing: the inherited store is byte-identical");
    CHECK(sentinel_reloads(existing), "existing: the sentinel key still reloads");

    /* --- fresh --- */
    char fresh_tmp[PATH_MAX];
    snprintf(fresh_tmp, sizeof(fresh_tmp), "%s.tmp", fresh);
    setenv("EMBEDIQ_NVM_PATH", fresh, 1);
    rc = run_child(cfg_bin);
    check_private_run("fresh", rc, fresh);
    CHECK(!exists(fresh) && !exists(fresh_tmp), "fresh: no file appears at the inherited path");

    /* --- unreadable --- */
    if (geteuid() == 0) {
        printf("SKIP  unreadable: running as root, a mode-000 file is readable\n");
    } else if (!write_all(unreadable, g_seed, seed_len) || chmod(unreadable, 0) != 0) {
        CHECK(false, "unreadable: prepare the mode-000 store");
    } else {
        setenv("EMBEDIQ_NVM_PATH", unreadable, 1);
        rc = run_child(cfg_bin);
        check_private_run("unreadable", rc, unreadable);
        struct stat st;
        CHECK(stat(unreadable, &st) == 0 && (st.st_mode & 0777) == 0,
              "unreadable: the inherited store keeps mode 000");
        (void)chmod(unreadable, 0600);
        len = read_all(unreadable, g_after);
        CHECK(len == seed_len && memcmp(g_seed, g_after, (size_t)seed_len) == 0,
              "unreadable: the inherited store is byte-identical");
    }

    /* --- setup fails: no private directory can be created --- */
    char missing[PATH_MAX];
    snprintf(missing, sizeof(missing), "%s.absent-dir", fresh);
    const char *tmpdir = getenv("TMPDIR");
    char saved_tmpdir[PATH_MAX] = {0};
    if (tmpdir) snprintf(saved_tmpdir, sizeof(saved_tmpdir), "%s", tmpdir);
    setenv("TMPDIR", missing, 1);
    setenv("EMBEDIQ_NVM_PATH", existing, 1);
    rc = run_child(cfg_bin);
    if (tmpdir) setenv("TMPDIR", saved_tmpdir, 1); else unsetenv("TMPDIR");
    CHECK(rc != 0 && rc != -1, "setup fails: the configuration test exits non-zero");
    CHECK(strstr(g_out, "PASS") == NULL, "setup fails: no check ran");
    len = read_all(existing, g_after);
    CHECK(len == seed_len && memcmp(g_seed, g_after, (size_t)seed_len) == 0,
          "setup fails: the inherited store is byte-identical");

    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return (g_failed > 0) ? 1 : 0;
}
