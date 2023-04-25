/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2019 Marcio Montenegro <mtuxpe@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/controller.h>
#include <zephyr/settings/settings.h>
#include "button_svc.h"
#include "led_svc.h"

/* Button value. */
static uint16_t but_val;

/* Prototype */
static ssize_t recv(struct bt_conn *conn,
		    const struct bt_gatt_attr *attr, const void *buf,
		    uint16_t len, uint16_t offset, uint8_t flags);

/* ST Custom Service  */
static struct bt_uuid_128 st_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe40, 0xcc7a, 0x482a, 0x984a, 0x7f2ed5b3e58f));

/* ST LED service */
static struct bt_uuid_128 led_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe41, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

/* ST Notify button service */
static struct bt_uuid_128 but_notif_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe42, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define DEVICE_PIN  123456
#define ADV_LEN 12

/* Advertising data */
static uint8_t manuf_data[ADV_LEN] = {
	0x01 /*SKD version */,
	0x83 /* STM32WB - P2P Server 1 */,
	0x00 /* GROUP A Feature  */,
	0x00 /* GROUP A Feature */,
	0x00 /* GROUP B Feature */,
	0x00 /* GROUP B Feature */,
	0x00, /* BLE MAC start -MSB */
	0x00,
	0x00,
	0x00,
	0x00,
	0x00, /* BLE MAC stop */
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manuf_data, ADV_LEN)
};

/* BLE connection */
struct bt_conn *gen_conn;
/* Notification state */
volatile bool notify_enable;
/* Authentication in progress? */
volatile bool authenticating = false;

static void mpu_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enable = (value == BT_GATT_CCC_NOTIFY);
	printk("Notification %s\n", notify_enable ? "enabled" : "disabled");
}

/* The embedded board is acting as GATT server.
 * The ST BLE Android app is the BLE GATT client.
 */

/* ST BLE Sensor GATT services and characteristic */

BT_GATT_SERVICE_DEFINE(stsensor_svc,
BT_GATT_PRIMARY_SERVICE(&st_service_uuid),
BT_GATT_CHARACTERISTIC(&led_char_uuid.uuid,
		       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		       BT_GATT_PERM_WRITE_AUTHEN, NULL, recv, (void *)1),
BT_GATT_CHARACTERISTIC(&but_notif_uuid.uuid, BT_GATT_CHRC_NOTIFY,
		       BT_GATT_PERM_READ_AUTHEN, NULL, NULL, &but_val),
BT_GATT_CCC(mpu_ccc_cfg_changed, BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
);

/* Functions for authentication */
static void auth_passkey_display(struct bt_conn *conn, unsigned int key) {
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Key for %s is %06u\n", addr, key);
}

static void auth_cancel(struct bt_conn *conn) {
	printk("Pairing canceled\n");
}

static void pairing_complete(struct bt_conn *conn, bool bonded) {
        char addr[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
        char addr[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}


/* Define the authentication callbacks */
static struct bt_conn_auth_cb conn_auth_callbacks = {
        .passkey_display = auth_passkey_display,
        .cancel = auth_cancel,
        .pairing_confirm = NULL
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
        .pairing_complete = pairing_complete,
        .pairing_failed = pairing_failed
};

/* Device callbacks */
static ssize_t recv(struct bt_conn *conn,
		    const struct bt_gatt_attr *attr, const void *buf,
		    uint16_t len, uint16_t offset, uint8_t flags)
{
	led_update();

	return 0;
}

static void button_callback(const struct device *gpiob, struct gpio_callback *cb,
		     uint32_t pins)
{
	int err;

	printk("Button pressed\n");
	if (gen_conn) {
		if (notify_enable) {
			err = bt_gatt_notify(NULL, &stsensor_svc.attrs[4],
					     &but_val, sizeof(but_val));
			if (err) {
				printk("Notify error: %d\n", err);
			} else {
				printk("Send notify ok\n");
				but_val = (but_val == 0) ? 0x100 : 0;
			}
		} else {
			printk("Notify not enabled\n");
		}
	} else {
		printk("BLE not connected\n");
	}
}

/* Start bluetooth stack */
static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
	printk("Bluetooth initialized\n");

	// bt_gatt_service_register(&stsensor_svc);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Configuration mode: waiting connections...\n");
}

/* connection callback */
static void connected(struct bt_conn *connected, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
	} else {
		printk("Connected\n");
		if (!gen_conn) {
			gen_conn = bt_conn_ref(connected);
		}

		err = bt_conn_set_security(gen_conn, BT_SECURITY_L4);
		if (err) {
			printk("Set security L4 failed\n");
		}
	}
}

/* disconnect callback */
static void disconnected(struct bt_conn *disconn, uint8_t reason)
{
	if (gen_conn) {
		bt_conn_unref(gen_conn);
		gen_conn = NULL;
	}

	printk("Disconnected (reason %u)\n", reason);
}

static void security_level_updated(struct bt_conn *conn, 
			bt_security_t level, enum bt_security_err err) {
	if (!err) {
		printk("Security changed to %d\n",level);
	} else {
		printk("Security failed now %d (%d)\n",level,err);
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_level_updated
};

int main(void)
{
	int err;
	unsigned int passkey = DEVICE_PIN;

	err = button_init(button_callback);
	if (err) {
		return 0;
	}

	err = led_init();
	if (err) {
		return 0;
	}

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&conn_auth_callbacks);
	bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	bt_passkey_set(passkey);
 
	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	return 0;
}
