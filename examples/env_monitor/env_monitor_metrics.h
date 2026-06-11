/*
 * examples/env_monitor/env_monitor_metrics.h — Metric and unit identifiers
 *
 * Application-specific metric IDs for the environmental monitor demo.
 * metric_id values are used in MSG_TELEMETRY_GAUGE_Payload_t.metric_id.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ENV_MONITOR_METRICS_H
#define ENV_MONITOR_METRICS_H

/** Metric IDs — application-local, no global registration needed. */
#define ENV_METRIC_TEMPERATURE   0x0001u  /**< °C  */
#define ENV_METRIC_HUMIDITY      0x0002u  /**< %RH */
#define ENV_METRIC_CO2           0x0003u  /**< ppm */
#define ENV_METRIC_ALERT_LEVEL   0x0004u  /**< 0=normal, 1=warning */

/** Unit IDs — informational (displayed in Grafana labels). */
#define ENV_UNIT_CELSIUS         0x01u
#define ENV_UNIT_PERCENT         0x02u
#define ENV_UNIT_PPM             0x03u
#define ENV_UNIT_NONE            0x00u

/** Temperature alert threshold (°C). Cloud can override via MSG_MQTT_CMD_RX. */
#define ENV_TEMP_ALERT_DEFAULT   27.0f

#endif /* ENV_MONITOR_METRICS_H */
