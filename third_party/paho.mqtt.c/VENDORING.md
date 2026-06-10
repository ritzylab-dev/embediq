# VENDORING.md — Eclipse Paho MQTT C

| Field                  | Value                                                                   |
|------------------------|-------------------------------------------------------------------------|
| name                   | paho.mqtt.c                                                             |
| version                | 1.3.13                                                                  |
| license                | EPL-2.0 / EDL-1.0 (Eclipse Distribution License — BSD-like, permissive)|
| soup_class             | B                                                                       |
| source_url             | https://github.com/eclipse/paho.mqtt.c/tree/v1.3.13                    |
| anomaly_list_url       | https://github.com/eclipse/paho.mqtt.c/issues                          |
| anomaly_assessment_date| 2026-06-02                                                              |

## Notes

soup_class B: established library, active maintenance. No critical CVEs applicable
to the synchronous MQTTClient API path used by EmbedIQ (client mode, TCP + TLS,
publish/subscribe). TLS is handled by mbedTLS via embediq_tls_ops_t — Paho's
own TLS support is NOT used (EmbedIQ routes TLS through the ops table).

## What is committed

Only: src/ (headers + C source), LICENSE, edl-v10.
Excluded: docs/, test/, src/samples/, build tools.

## Source status

Committed in fb_cloud_mqtt PR-B. Used by hal/posix/ops/hal_mqtt_posix.c
(implemented in PR-C). FBs never include Paho headers (D-LIB-4).

To reproduce at the pinned version:
  git clone https://github.com/eclipse/paho.mqtt.c --branch v1.3.13 --depth 1
