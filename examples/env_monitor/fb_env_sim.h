/*
 * examples/env_monitor/fb_env_sim.h — Environmental Sensor Simulator FB API
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef FB_ENV_SIM_H
#define FB_ENV_SIM_H
#include "embediq_fb.h"
#ifdef __cplusplus
extern "C" {
#endif
EmbedIQ_FB_Handle_t fb_env_sim_register(void);
#ifdef __cplusplus
}
#endif
#endif /* FB_ENV_SIM_H */
