/*
 * platform/posix/embediq_platform_msgs.h — compatibility shim
 *
 * Message IDs have moved to core/include/embediq_platform_msgs.h so that
 * application FBs can subscribe to platform messages without including a
 * platform-layer header (layer boundary rule, AGENTS.md §3).
 *
 * This file is kept as a shim so that platform/posix source files that
 * include it directly continue to compile unchanged.
 *
 * Uses a relative path to avoid a circular include: if this file used
 * #include "embediq_platform_msgs.h", the compiler would find this shim
 * again (it is in the same directory and is searched first).
 *
 * DO NOT add new definitions here. Edit core/include/embediq_platform_msgs.h.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../core/include/embediq_platform_msgs.h"
