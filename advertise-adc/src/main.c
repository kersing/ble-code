/*
 * Advertise LMT86 temperature data 
 *
 * Copyright (c) 2023 The-Box Development
 * Based on an example by Koen Vervloesem 
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

#include "lmt86.h"

#define ADV_PARAM                                                    \
  BT_LE_ADV_PARAM(0, BT_GAP_ADV_SLOW_INT_MIN,                        \
                  BT_GAP_ADV_SLOW_INT_MAX, NULL)

static struct bt_data advertisement[] = 
{
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(
        BT_DATA_MANUFACTURER_DATA, 0xff, 0xff, /* Test company ID */
        0x00, 0x00, 0x00 /* Temperature, packed bcd encoded (because we can) */
    )
};

void update_data(const struct adc_dt_spec *d) 
{
  int16_t temp;
  char s[4];

  temp = lmt86_get_temperature(d);
  // Use binary format for maximum efficiency when using other software to process
  // memcpy(&(advertisement[1].data[2]), &temp, 2);

  // Put it in BCD format for easy human consumption in nRF explorer
  if (temp < 0)
  {
	  s[0] = '-';
	  temp = 0 - temp;
  }
  else
  {
	  s[0]= 0;
  }

  // Limit range to 4 digit
  temp = temp % 10000;

  s[1]= ( temp / 1000 ) << 4; 
  temp = temp % 1000;
  s[1]= s[1] | ( temp / 100 );
  temp = temp % 100;
  s[2]= ( temp / 10 ) << 4; 
  temp = temp % 10;
  s[2]= s[2] | ( temp );
  s[3]= '\0';
  memcpy(&(advertisement[1].data[2]), &s, 3);
}

void main(void) 
{
  int err;

  printk("Startup\n");

  // Get ADC device
  const struct adc_dt_spec *lmt86 = get_device_lmt86();

  if (lmt86 == NULL) 
  {
    printk("Failed to get ADC device\n");
    return;
  }

  // Initialize the Bluetooth subsystem
  err = bt_enable(NULL);
  if (err) 
  {
    printk("Bluetooth initialize failed (%d)\n", err);
    return;
  }

  printk("Bluetooth init done\n");

  // Start advertising sensor values
  update_data(lmt86);
  err = bt_le_adv_start(ADV_PARAM, advertisement, ARRAY_SIZE(advertisement), NULL, 0);

  if (err) 
  {
    printk("Advertising start failed (%d)\n", err);
    return;
  }

  while (1) 
  {
    k_sleep(K_MSEC(970));
    update_data(lmt86);
    err = bt_le_adv_update_data(advertisement, ARRAY_SIZE(advertisement), NULL, 0);

    if (err) 
    {
      printk("Advertising data update failed (%d)\n", err);
      return;
    }
  }
}
