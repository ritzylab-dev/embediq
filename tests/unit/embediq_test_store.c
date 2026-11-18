#define _POSIX_C_SOURCE 200809L   /* mkdtemp, setenv */
#ifdef __APPLE__
#  define _DARWIN_C_SOURCE        /* macOS declares mkdtemp only outside strict POSIX */
#endif

/*
 * tests/unit/embediq_test_store.c — private NVM store for one test process
 *
 * See embediq_test_store.h. The directory comes from mkdtemp(), so its name
 * is unique and it is created empty with mode 0700; nothing outside it is
 * ever written or removed.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_test_store.h"

#include <limits.h>   /* PATH_MAX */
#include <stdio.h>
#include <stdlib.h>   /* mkdtemp, setenv, getenv, atexit, exit */
#include <string.h>
#include <unistd.h>   /* unlink, rmdir */

/* Named files per process: the store itself plus every name a test passes to
 * embediq_test_store_file(). The largest user names one extra file, so four
 * slots leave room without a list to grow. */
#define TEST_STORE_MAX_FILES  4

static char s_dir[PATH_MAX];
static char s_files[TEST_STORE_MAX_FILES][PATH_MAX];
static int  s_file_count = 0;
static int  s_selected   = 0;

static void fail(const char *what)
{
    fprintf(stderr, "test store: %s — refusing to run without a private store\n", what);
    exit(2);
}

/* Remove "<path>" and "<path>.tmp" if present; a missing file is not an error. */
static void remove_with_temp(const char *path)
{
    char tmp[PATH_MAX];
    (void)unlink(path);
    int rc = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (rc > 0 && (size_t)rc < sizeof(tmp)) {
        (void)unlink(tmp);
    }
}

static void cleanup(void)
{
    for (int i = 0; i < s_file_count; i++) {
        remove_with_temp(s_files[i]);
    }
    (void)rmdir(s_dir);   /* fails, harmlessly, if a test left another file */
}

/* Record "<dir>/<name>" in the next slot and return it. */
static const char *add_file(const char *name)
{
    if (s_file_count >= TEST_STORE_MAX_FILES) fail("too many store files");
    char *slot = s_files[s_file_count];
    /* Room for "<path>.tmp" too, so the companion name is never shortened. */
    int rc = snprintf(slot, PATH_MAX - sizeof(".tmp"), "%s/%s", s_dir, name);
    if (rc < 0 || (size_t)rc >= PATH_MAX - sizeof(".tmp")) fail("store path too long");
    s_file_count++;
    return slot;
}

const char *embediq_test_store_select(void)
{
    if (s_selected) return s_files[0];

    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') base = "/tmp";
    size_t blen = strlen(base);
    const char *sep = (blen > 0u && base[blen - 1u] == '/') ? "" : "/";
    int rc = snprintf(s_dir, sizeof(s_dir), "%s%sembediq-test-store-XXXXXX", base, sep);
    if (rc < 0 || (size_t)rc >= sizeof(s_dir)) fail("directory path too long");
    if (mkdtemp(s_dir) == NULL) {
        perror("test store: mkdtemp");
        fail("cannot create a private directory");
    }
    if (atexit(cleanup) != 0) {
        (void)rmdir(s_dir);
        fail("cannot register cleanup");
    }

    const char *store = add_file("nvm_store.bin");
    if (setenv("EMBEDIQ_NVM_PATH", store, 1) != 0) fail("cannot set EMBEDIQ_NVM_PATH");
    s_selected = 1;
    printf("test store: %s\n", store);
    fflush(stdout);
    return store;
}

const char *embediq_test_store_file(const char *name)
{
    (void)embediq_test_store_select();
    return add_file(name);
}
