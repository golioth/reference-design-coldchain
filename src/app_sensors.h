/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __APP_SENSORS_H__
#define __APP_SENSORS_H__

#include <golioth/client.h>

void app_sensors_set_client(struct golioth_client *sensors_client);
void app_sensors_read_and_stream(void);
void app_sensors_init(void);

#define LABEL_LAT	"Latitude"
#define LABEL_LON	"Longitude"
#define LABEL_TEM	"Temperature"
#define LABEL_BATTERY	"Battery"
#define LABEL_FIRMWARE	"Firmware"
#define SUMMARY_TITLE	"Cold Chain"

/**
 * Each Ostentus slide needs a unique key. You may add additional slides by
 * inserting elements with the name of your choice to this enum.
 */
typedef enum {
	SLIDE_LAT,
	SLIDE_LON,
	SLIDE_TEM,
#ifdef CONFIG_ALUDEL_BATTERY_MONITOR
	BATTERY_V,
	BATTERY_LVL,
#endif
	FIRMWARE
} slide_key;

#endif /* __APP_SENSORS_H__ */
