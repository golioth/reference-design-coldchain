/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensors, LOG_LEVEL_DBG);

#include <golioth/client.h>
#include <golioth/stream.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>

#include "app_sensors.h"
#include "app_settings.h"
#include "lib/minmea/minmea.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef CONFIG_LIB_OSTENTUS
#include <libostentus.h>
#endif
#ifdef CONFIG_ALUDEL_BATTERY_MONITOR
#include "battery_monitor/battery.h"
#endif

#define UART_DEVICE_NODE DT_ALIAS(click_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

#define UART_SEL DT_ALIAS(gnss7_sel)
static const struct gpio_dt_spec gnss7_sel = GPIO_DT_SPEC_GET(UART_SEL, gpios);

#define NMEA_SIZE 128

/* Max number of parsed readings to queue between uploads (96 bytes each) */
#define MAX_QUEUED_DATA 500
#define GPS_BATCH_STREAM_TIMEOUT_S 2

/* GPS stream endpoint on Golioth */
#define GPS_ENDP "gps"

struct cold_chain_data {
	struct sensor_value tem;
	struct sensor_value pre;
	struct sensor_value hum;
	struct minmea_sentence_rmc frame;
};

struct weather_data {
	struct sensor_value tem;
	struct sensor_value pre;
	struct sensor_value hum;
};

#define ERROR_VAL1 999
#define ERROR_VAL2 999999
static const struct sensor_value reading_error = {
	.val1 = ERROR_VAL1,
	.val2 = ERROR_VAL2
};

/* Global to hold BME280 readings; updated at 1 Hz by thread */
struct weather_data _latest_weather_data = {
	.tem.val1 = ERROR_VAL1, .tem.val2 = ERROR_VAL2,
	.pre.val1 = ERROR_VAL1, .pre.val2 = ERROR_VAL2,
	.hum.val1 = ERROR_VAL1, .hum.val2 = ERROR_VAL2,
};

/* Processed data waiting to be sent to Golioth */
K_MSGQ_DEFINE(coldchain_msgq, sizeof(struct cold_chain_data), MAX_QUEUED_DATA, 4);

static char rx_buf[NMEA_SIZE];
static int rx_buf_pos;

static struct golioth_client *client;
/* Add Sensor structs here */
const struct device *weather_dev;

/* Thread reads weather sensor and provides easy access to latest data */
K_MUTEX_DEFINE(weather_mutex);		    /* Protect data */
K_SEM_DEFINE(bme280_initialized_sem, 0, 1); /* Wait until sensor is ready */

void weather_sensor_data_fetch(void)
{
	if (!weather_dev) {
		_latest_weather_data.tem = reading_error;
		_latest_weather_data.pre = reading_error;
		_latest_weather_data.hum = reading_error;
		return;
	}
	sensor_sample_fetch(weather_dev);
	k_mutex_lock(&weather_mutex, K_FOREVER);

	sensor_channel_get(weather_dev, SENSOR_CHAN_AMBIENT_TEMP, &_latest_weather_data.tem);
	sensor_channel_get(weather_dev, SENSOR_CHAN_PRESS, &_latest_weather_data.pre);
	sensor_channel_get(weather_dev, SENSOR_CHAN_HUMIDITY, &_latest_weather_data.hum);

	k_mutex_unlock(&weather_mutex);
}

#define WEATHER_STACK 1024

extern void weather_sensor_thread(void *d0, void *d1, void *d2)
{
	/* Block until sensor is available */
	k_sem_take(&bme280_initialized_sem, K_FOREVER);
	while (1) {
		weather_sensor_data_fetch();
		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(weather_sensor_tid, WEATHER_STACK, weather_sensor_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/** @brief Check if a given time delay has passed
 *
 * The stored start time is used to generate a delta time from the system clock. If the delta time
 * is smaller than the given delay, this function returns false. If the result is true, the stored
 * value may be replaced by the current system time using the update param.
 *
 * @param stored_start_time stored system time in ms used as start time
 * @param delay_s delay (in seconds)
 * @param update_stored_time if function if returning true, this enables current time to replace the
 * stored time value.
 *
 * @return true if gps_delay_s has passed since last GPS reading, otherwise false
 */
static bool target_time_elapsed(uint64_t *stored_start_time, uint32_t delay_s,
				bool update_stored_time)
{
	uint64_t wait_for = *stored_start_time;

	if (k_uptime_delta(&wait_for) >= ((uint64_t)delay_s * 1000)) {
		if (update_stored_time) {
			/* wait_for now contains the current timestamp. Store this for the next
			 * reading.
			 */
			*stored_start_time = wait_for; }
		return true;
	}
	return false;
}

#ifdef CONFIG_LIB_OSTENTUS
static void update_ostentus_gps(float lat, float lon, char *tem_str, char tem_len)
{
	char lat_str[12];
	char lon_str[12];

	snprintk(lat_str, sizeof(lat_str), "%f", lat);
	snprintk(lon_str, sizeof(lon_str), "%f", lon);

	slide_set(SLIDE_LAT, lat_str, strlen(lat_str));
	slide_set(SLIDE_LON, lon_str, strlen(lon_str));
	slide_set(SLIDE_TEM, tem_str, tem_len);
}
#endif

/* Raw string data waiting for the NMEA parser to run */
K_MSGQ_DEFINE(reading_msgq, NMEA_SIZE, 16, 4);

#define PARSER_STACK 1024

extern void nmea_parser_thread(void *d0, void *d1, void *d2)
{
	char raw_readings[NMEA_SIZE];
	enum minmea_sentence_id sid;
	bool sat_lock = false;
	int err;

	/* timestamp when the previous GPS value was stored */
	uint64_t last_gps = 0;

	/* timestamp when the previous satellite lock message was sent */
	uint64_t last_sat_msg = 0;

	while (1) {
		k_msgq_get(&reading_msgq, &raw_readings, K_FOREVER);
		sid = minmea_sentence_id(raw_readings, false);

		if (!sat_lock && sid == MINMEA_SENTENCE_GSV) {

			if (target_time_elapsed(&last_sat_msg, 3, true)) {
				struct minmea_sentence_gsv lock_frame;

				minmea_parse_gsv(&lock_frame, raw_readings);
				LOG_INF("Awaiting GPS lock. Satellite count: %d",
					lock_frame.total_sats);
			}
		}

		if (sid != MINMEA_SENTENCE_RMC) {
			/* We only care about RMC sentences because that's the data we need */
			continue;
		}

		struct cold_chain_data cc_data;
		bool success = minmea_parse_rmc(&cc_data.frame, raw_readings);

		if (!success) {
			/* Failed to partse NMEA sentence */
			continue;
		}

		sat_lock = cc_data.frame.valid;

		if (!sat_lock) {
			continue;
		}

		if (!target_time_elapsed(&last_gps, get_gps_delay_s(), true)) {
			/* gps_delay_s has not elapsed since last reading */
			continue;
		}

		err = k_mutex_lock(&weather_mutex, K_MSEC(50));

		if (err) {
			LOG_ERR("Cannot access weather data: %d", err);
			/* Use an obvious error value so these are not used uninitialized */
			cc_data.tem = reading_error;
			cc_data.pre = reading_error;
			cc_data.hum = reading_error;
		} else {
			cc_data.tem = _latest_weather_data.tem;
			cc_data.pre = _latest_weather_data.pre;
			cc_data.hum = _latest_weather_data.hum;
			k_mutex_unlock(&weather_mutex);
		}

		err = k_msgq_put(&coldchain_msgq, &cc_data, K_MSEC(1));

		if (err) {
			LOG_ERR("Unable to queue parsed coldchain data: %d", err);
		} else {
			char tem_str[12];

			snprintk(tem_str, sizeof(tem_str), "%d.%02dc",
				 cc_data.tem.val1,
				 cc_data.tem.val2 / 10000);

			LOG_DBG("nmea: %s t: %s", raw_readings, tem_str);

			IF_ENABLED(CONFIG_LIB_OSTENTUS, (
				update_ostentus_gps(minmea_tocoord(&cc_data.frame.latitude),
						    minmea_tocoord(&cc_data.frame.longitude),
						    tem_str, strlen(tem_str));
			));

			uint32_t msg_cnt = k_msgq_num_used_get(&coldchain_msgq);

			if (msg_cnt > 0 && (msg_cnt % 5 == 0)) {
				LOG_INF("%d readings queued; %d slots remain", msg_cnt,
					MAX_QUEUED_DATA - msg_cnt);
			}
		}
	}
}

K_THREAD_DEFINE(nmea_parser_tid, PARSER_STACK, nmea_parser_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/* UART callback */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	while (uart_irq_rx_ready(uart_dev)) {

		uart_fifo_read(uart_dev, &c, 1);

		if ((c == '\n') && rx_buf_pos > 0) {
			/* terminate string */
			if (rx_buf_pos == (NMEA_SIZE - 1)) {
				rx_buf[rx_buf_pos] = '\0';
			} else {
				rx_buf[rx_buf_pos] = '\n';
				rx_buf[rx_buf_pos + 1] = '\0';
			}

			if (k_msgq_put(&reading_msgq, &rx_buf, K_NO_WAIT) != 0) {
				LOG_ERR("Message queue full, dropping reading.");
			}

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/*
 * Get a device structure from a devicetree node with compatible
 * "bosch,bme280". (If there are multiple, just pick one.)
 */
static const struct device *get_bme280_device(void)
{
	const struct device *const bme_dev = DEVICE_DT_GET_ANY(bosch_bme280);

	if (bme_dev == NULL) {
		/* No such node, or the node does not have status "okay". */
		LOG_ERR("\nError: no device found.");
		return NULL;
	}

	if (!device_is_ready(bme_dev)) {
		LOG_ERR("Error: Device \"%s\" is not ready; "
			"check the driver initialization logs for errors.",
			bme_dev->name);
		return NULL;
	}

	LOG_DBG("Found device \"%s\", getting sensor data", bme_dev->name);

	/* Give semaphore to signal sensor is ready for reading */
	k_sem_give(&bme280_initialized_sem);
	return bme_dev;
}

static void get_sensor_string_or_empty(struct sensor_value v, char *buf, uint8_t buf_len, char *key)
{
	if ((v.val1 == reading_error.val1) && (v.val2 == reading_error.val2)) {
		buf[0] = '\0';
	} else {
		snprintk(buf, buf_len, ",\"%s\":%d.%06d", key, v.val1, abs(v.val2));
	}
}

static void batch_upload_to_golioth(void)
{
	uint32_t msg_cnt = k_msgq_num_used_get(&coldchain_msgq);

	if (msg_cnt == 0) {
		return;
	}

	LOG_INF("Uploading cached data to Golioth");

	/* Packets will be no larger than 1024 characters
	 *
	 * {"lat":-90.123456,"lon":-180.123456,"time":"2023-09-18T22:52:42.000Z","tem":-100.123456,
	 *  "pre":-100.123456,"hum":-100.123456}"
	 *
	 * Round up for the comma, and overall open/close bracket.
	 * Size the overall buffer to be smaller than 1024 which is most efficient for Golioth
	 */
	static const char r_fmt[] = "{\"lat\":%f,\"lon\":%f,\""
				    "time\":\"20%02d-%02d-%02dT%02d:%02d:%02d.%03dZ\""
				    "%s%s%s}";
	const uint8_t r_maxlen = 128;
	const uint16_t buf_len = 1000;
	uint16_t remaining_len;
	uint16_t tot_pushed = 0;
	char buf[buf_len];
	struct cold_chain_data cached_data;

	snprintk(buf, sizeof(buf), "%s", "[");

	while (msg_cnt > 0) {
		int err = k_msgq_get(&coldchain_msgq, &cached_data, K_MSEC(10));

		if (err) {
			LOG_ERR("Error fetching cached reading: %d", err);
			return;
		}

		char tem_str[19], pre_str[19], hum_str[19];

		get_sensor_string_or_empty(cached_data.tem, tem_str, sizeof(tem_str), "tem");
		get_sensor_string_or_empty(cached_data.pre, pre_str, sizeof(pre_str), "pre");
		get_sensor_string_or_empty(cached_data.hum, hum_str, sizeof(hum_str), "hum");

		snprintk(buf + strlen(buf), buf_len - strlen(buf), r_fmt,
			 minmea_tocoord(&cached_data.frame.latitude),
			 minmea_tocoord(&cached_data.frame.longitude),
			 cached_data.frame.date.year, cached_data.frame.date.month,
			 cached_data.frame.date.day, cached_data.frame.time.hours,
			 cached_data.frame.time.minutes, cached_data.frame.time.seconds,
			 cached_data.frame.time.microseconds,
			 tem_str, pre_str, hum_str);

		msg_cnt = k_msgq_num_used_get(&coldchain_msgq);
		remaining_len = sizeof(buf) - strlen(buf) - 1;
		tot_pushed++;

		if (msg_cnt == 0 || remaining_len < r_maxlen) {
			snprintk(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s", "]");

			int err = golioth_stream_set_sync(client,
							  GPS_ENDP,
							  GOLIOTH_CONTENT_TYPE_JSON,
							  buf,
							  strlen(buf),
							  GPS_BATCH_STREAM_TIMEOUT_S);

			if (err) {
				LOG_ERR("Failed to send sensor data to Golioth: %d", err);
				return;
			}

			if (msg_cnt) {
				snprintk(buf, sizeof(buf), "%s", "[");
			}
		} else {
			snprintk(buf + strlen(buf), buf_len - strlen(buf), "%s", ",");
		}
	}

	LOG_INF("Pushed %d cached readings up to Golioth.", tot_pushed);
}

/* This will be called by the main() loop */
/* Do all of your work here! */
void app_sensors_read_and_stream(void)
{
	IF_ENABLED(CONFIG_ALUDEL_BATTERY_MONITOR, (
		read_and_report_battery(client);
		IF_ENABLED(CONFIG_LIB_OSTENTUS, (
			slide_set(BATTERY_V, get_batt_v_str(), strlen(get_batt_v_str()));
			slide_set(BATTERY_LVL, get_batt_lvl_str(), strlen(get_batt_lvl_str()));
		));
	));

	if (golioth_client_is_connected(client)) {
		batch_upload_to_golioth();
	}
}

void app_sensors_set_client(struct golioth_client *sensors_client)
{
	client = sensors_client;
}

void app_sensors_init(void)
{
	LOG_INF("Initializing UART");

	int err = gpio_pin_configure_dt(&gnss7_sel, GPIO_OUTPUT_ACTIVE);

	if (err < 0) {
		LOG_ERR("Unable to configure GNSS SEL Pin: %d", err);
	}

	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	weather_dev = get_bme280_device();
	weather_sensor_data_fetch();
}
