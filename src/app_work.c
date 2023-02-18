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
K_MSGQ_DEFINE(nmea_msgq, NMEA_SIZE, 16, 4);

static char rx_buf[NMEA_SIZE];
static int rx_buf_pos;

static struct golioth_client *client;
/* Add Sensor structs here */

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

/* This will be called by the main() loop */
/* Do all of your work here! */
void app_work_sensor_read(void) {
	int err;
	char received_nmea[NMEA_SIZE];
	static struct minmea_sentence_rmc frame;
	char json_buf[128];

	while (k_msgq_get(&nmea_msgq, &received_nmea, K_NO_WAIT) == 0) {
		bool success = minmea_parse_rmc(&frame, received_nmea);
		if (success) {
			
			float lat = minmea_tocoord(&frame.latitude);
			float lon = minmea_tocoord(&frame.longitude);
			if ((lat == NAN) || (lon == NAN)) {
				LOG_DBG("Skipping because lat or lon is NAN");
				continue;
			}
			snprintf(json_buf, sizeof(json_buf),
					"{\"lat\":%f,\"lon\":%f,\"alt\":0,\"time\":\"%02d-%02d-%02dT%02d:%02d:%02d.%03dZ\"}",
					lat,
					lon,
					frame.date.year,
					frame.date.month,
					frame.date.day,
					frame.time.hours,
					frame.time.minutes,
					frame.time.seconds,
					frame.time.microseconds
					);
			LOG_INF("%s", json_buf);
			err = golioth_stream_push_cb(client, "gps",
					GOLIOTH_CONTENT_FORMAT_APP_JSON,
					json_buf, strlen(json_buf),
					async_error_handler, NULL);
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
}

