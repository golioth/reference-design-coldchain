/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_work, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>

#include "app_work.h"
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

/* Global timestamp records when the previous GPS value was stored */
uint64_t _last_gps = 0;

/* Global to hold BME280 readings; updated at 1 Hz by thread */
struct weather_data _latest_weather_data;

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

/**
 * @brief Check of gps_delay_s has passed since the last GPS reading was taken.
 *
 * @param update_stored_time if this param is true, and the return value is true, current time will
 * be stored as the most recent GPS reading.
 *
 * @return true if gps_delay_s has passed since last GPS reading, otherwise false
 */
bool time_for_gps_reading(bool update_stored_time)
{
	uint64_t wait_for = _last_gps;

	if (k_uptime_delta(&wait_for) >= ((uint64_t)get_gps_delay_s() * 1000)) {
		if (update_stored_time) {
			/* wait_for now contains the current timestamp. Store this
			 * for the next reading.
			 */
			_last_gps = wait_for;
		}

		return true;
	}
	return false;
}


/* Raw string data waiting for the NMEA parser to run */
K_MSGQ_DEFINE(reading_msgq, NMEA_SIZE, 16, 4);

#define PARSER_STACK 1024

extern void nmea_parser_thread(void *d0, void *d1, void *d2)
{
	char raw_readings[NMEA_SIZE];
	enum minmea_sentence_id sid;
	int err;

	while (1) {
		k_msgq_get(&reading_msgq, &raw_readings, K_FOREVER);
		sid = minmea_sentence_id(raw_readings, false);

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

		if (cc_data.frame.valid != true) {
			/* TODO: no satellite lock, log info about tracking progress */
			continue;
		}

		if (!time_for_gps_reading(true)) {
			/* gps_delay_s has not elapsed since last reading */
			continue;
		}

		err = k_mutex_lock(&weather_mutex, K_MSEC(50));

		if (err) {
			LOG_ERR("Cannot access weather data: %d", err);
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
			LOG_DBG("nmea: %s t: %d.%02dc",
				raw_readings,
				cc_data.tem.val1,
				cc_data.tem.val2 / 10000);

			/* FIXME
			 * slide_set(O_LAT, lat_str, strlen(lat_str));
			 * slide_set(O_LON, lon_str, strlen(lon_str));
			 * slide_set(O_TEM, tem_str, strlen(tem_str));
			 */

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
				    "time\":\"20%02d-%02d-%02dT%02d:%02d:%02d.%03dZ\","
			     	    "\"tem\":%d.%06d,\"pre\":%d.%06d,\"hum\":%d.%06d}";
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

		snprintk(buf + strlen(buf), buf_len - strlen(buf), r_fmt,
			 minmea_tocoord(&cached_data.frame.latitude),
			 minmea_tocoord(&cached_data.frame.longitude),
			 cached_data.frame.date.year, cached_data.frame.date.month,
			 cached_data.frame.date.day, cached_data.frame.time.hours,
			 cached_data.frame.time.minutes, cached_data.frame.time.seconds,
			 cached_data.frame.time.microseconds,
			 cached_data.tem.val1, abs(cached_data.tem.val2),
			 cached_data.pre.val1, abs(cached_data.pre.val2),
			 cached_data.hum.val1, abs(cached_data.hum.val2));

		msg_cnt = k_msgq_num_used_get(&coldchain_msgq);
		remaining_len = sizeof(buf) - strlen(buf) - 1;
		tot_pushed++;

		if (msg_cnt == 0 || remaining_len < r_maxlen) {
			snprintk(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s", "]");
			int err = golioth_stream_push(client, GPS_ENDP,
						      GOLIOTH_CONTENT_FORMAT_APP_JSON,
						      buf, strlen(buf));

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
void app_work_sensor_read(void)
{
	IF_ENABLED(CONFIG_ALUDEL_BATTERY_MONITOR, (
		read_and_report_battery();
		IF_ENABLED(CONFIG_LIB_OSTENTUS, (
			slide_set(BATTERY_V, get_batt_v_str(), strlen(get_batt_v_str()));
			slide_set(BATTERY_LVL, get_batt_lvl_str(), strlen(get_batt_lvl_str()));
		));
	));

	if (golioth_is_connected(client)) {
		batch_upload_to_golioth();
	}
}

void app_work_init(struct golioth_client *work_client)
{
	LOG_INF("Initializing UART");
	client = work_client;

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
