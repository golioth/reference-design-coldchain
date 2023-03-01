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
#include "libostentus/libostentus.h"
#include "lib/minmea/minmea.h"
#include <stdio.h>

#define UART_DEVICE_NODE DT_ALIAS(click_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

#define NMEA_SIZE 128
K_MSGQ_DEFINE(nmea_msgq, NMEA_SIZE, 64, 4);

static char rx_buf[NMEA_SIZE];
static int rx_buf_pos;

static struct golioth_client *client;
/* Add Sensor structs here */
const struct device *weather_dev;

/* Formatting string for sending sensor JSON to Golioth */
#define JSON_FMT	"{\"counter\":%d}"

/* UART callback */
void serial_cb(const struct device *dev, void *user_data) {
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
				rx_buf[rx_buf_pos+1] = '\0';
			}

			/* if queue is full, message is silently dropped */
			enum minmea_sentence_id sid;
			sid = minmea_sentence_id(rx_buf, false);
			if (sid == MINMEA_SENTENCE_RMC) {
				k_msgq_put(&nmea_msgq, &rx_buf, K_NO_WAIT);
			}

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/* Callback for LightDB Stream */
static int async_error_handler(struct golioth_req_rsp *rsp) {
	if (rsp->err) {
		LOG_ERR("Async task failed: %d", rsp->err);
		return rsp->err;
	}
	return 0;
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
	return bme_dev;
}

/* This will be called by the main() loop */
/* Do all of your work here! */
void app_work_sensor_read(void) {
	int err;
	struct sensor_value tem, pre, hum;
	char received_nmea[NMEA_SIZE];
	static struct minmea_sentence_rmc frame;
	char json_buf[128];
	char ts_str[32];
	char lat_str[12];
	char lon_str[12];

	sensor_sample_fetch(weather_dev);
	sensor_channel_get(weather_dev, SENSOR_CHAN_AMBIENT_TEMP, &tem);
	sensor_channel_get(weather_dev, SENSOR_CHAN_PRESS, &pre);
	sensor_channel_get(weather_dev, SENSOR_CHAN_HUMIDITY, &hum);

	LOG_INF("Temperature: %d.%06d Pressure: %d.%06d Humidity: %d.%06d",
			tem.val1, tem.val2,
			pre.val1, pre.val2,
			hum.val1, hum.val2
			);

	while (k_msgq_get(&nmea_msgq, &received_nmea, K_NO_WAIT) == 0) {
		bool success = minmea_parse_rmc(&frame, received_nmea);
		if (success) {
			
			if (!frame.valid) {
				LOG_DBG("Skipping because satellite fix not established");
				continue;
			}

			snprintf(lat_str, sizeof(lat_str), "%f", minmea_tocoord(&frame.latitude));
			snprintf(lon_str, sizeof(lon_str), "%f", minmea_tocoord(&frame.longitude));
			snprintf(ts_str, sizeof(ts_str), "20%02d-%02d-%02dT%02d:%02d:%02d.%03dZ",
					frame.date.year,
					frame.date.month,
					frame.date.day,
					frame.time.hours,
					frame.time.minutes,
					frame.time.seconds,
					frame.time.microseconds
					);

			snprintk(json_buf, sizeof(json_buf),
					"{\"lat\":%s,\"lon\":%s,\"alt\":0,\"time\":\"%s\"}",
					lat_str,
					lon_str,
					ts_str
					);
			LOG_DBG("%s", json_buf);
			slide_set(O_LAT, lat_str, strlen(lat_str));
			slide_set(O_LON, lon_str, strlen(lon_str));

			err = golioth_stream_push(client, "gps",
					GOLIOTH_CONTENT_FORMAT_APP_JSON,
					json_buf, strlen(json_buf));
			if (err) LOG_ERR("Failed to send sensor data to Golioth: %d", err);	
		}
	}
}

void app_work_init(struct golioth_client* work_client) {
	LOG_INF("Initializing UART");
	client = work_client;
	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	weather_dev = get_bme280_device();
}

