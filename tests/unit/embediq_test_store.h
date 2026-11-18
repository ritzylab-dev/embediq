/*
 * tests/unit/embediq_test_store.h — private NVM store for one test process
 *
 * A test that reaches the real file-backed flash HAL calls
 * embediq_test_store_select() before its first NVM or flash operation. The
 * store then lives in a directory this process created, never at an inherited
 * EMBEDIQ_NVM_PATH and never at the default under $HOME, so a test run can
 * neither read nor overwrite a store it does not own, and concurrent runs
 * (normal, ThreadSanitizer, AddressSanitizer) never share a file.
 *
 * Test code only: linked into test executables, never into a framework
 * library.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_TEST_STORE_H
#define EMBEDIQ_TEST_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a new private directory under $TMPDIR (or /tmp when TMPDIR is unset
 * or empty), set EMBEDIQ_NVM_PATH to "<dir>/nvm_store.bin" — replacing any
 * inherited value — and return that path. Prints "test store: <path>" once.
 *
 * At exit, removes only what this process created there: the store, its
 * ".tmp" companion, each file named through embediq_test_store_file() and
 * its ".tmp" companion, then the directory (left in place if anything else
 * is in it).
 *
 * If the directory cannot be created or the variable cannot be set, prints
 * the reason to stderr and exits with status 2: there is no fallback to the
 * inherited or default path. Idempotent: later calls return the same path.
 */
const char *embediq_test_store_select(void);

/**
 * Return "<dir>/<name>" inside the private directory (selecting it first if
 * needed), for a test that needs a second store file. The file is removed at
 * exit like the store. Exits with status 2 if the path does not fit or the
 * per-process list of named files is full.
 */
const char *embediq_test_store_file(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TEST_STORE_H */
