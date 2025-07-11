/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_DBG);

#include <golioth/client.h>
#include <golioth/settings.h>
#include "main.h"
#include "app_settings.h"

/* How long to wait between uploading to Golioth */
static int32_t _loop_delay_s = 5;
#define LOOP_DELAY_S_MAX 43200
#define LOOP_DELAY_S_MIN 1

/* How long to wait between GPS readings */
static int32_t _gps_delay_s = 3;
#define GPS_DELAY_S_MAX 43200
#define GPS_DELAY_S_MIN 0

int32_t get_loop_delay_s(void)
{
	return _loop_delay_s;
}

int32_t get_gps_delay_s(void) {
	return _gps_delay_s;
}

static enum golioth_settings_status on_loop_delay_setting(int32_t new_value, void *arg)
{
	/* Only update if value has changed */
	if (_loop_delay_s == new_value) {
		LOG_DBG("Received LOOP_DELAY_S already matches local value.");
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	_loop_delay_s = new_value;
	LOG_INF("Set loop delay to %i seconds", new_value);
	wake_system_thread();
	return GOLIOTH_SETTINGS_SUCCESS;
}

static enum golioth_settings_status on_gps_delay_setting(int32_t new_value, void *arg)
{
	/* Only update if value has changed */
	if (_gps_delay_s == new_value) {
		LOG_DBG("Received GPS_DELAY_S already matches local value.");
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	_gps_delay_s = new_value;
	LOG_INF("Set gps delay to %i seconds", new_value);
	wake_system_thread();
	return GOLIOTH_SETTINGS_SUCCESS;
}

void app_settings_register(struct golioth_client *client)
{
	struct golioth_settings *settings = golioth_settings_init(client);

	int err = golioth_settings_register_int_with_range(settings,
							   "LOOP_DELAY_S",
							   LOOP_DELAY_S_MIN,
							   LOOP_DELAY_S_MAX,
							   on_loop_delay_setting,
							   NULL);

	if (err) {
		LOG_ERR("Failed to register settings callback: %d", err);
	}

	err = golioth_settings_register_int_with_range(settings,
							   "GPS_DELAY_S",
							   GPS_DELAY_S_MIN,
							   GPS_DELAY_S_MAX,
							   on_gps_delay_setting,
							   NULL);

	if (err) {
		LOG_ERR("Failed to register settings callback: %d", err);
	}
}
