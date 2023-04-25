/*
 *
 * LMT86 
 *
 * Copyright (c) 2023 The-Box Development
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "lmt86.h"

const struct adc_dt_spec *get_device_lmt86()
{
	int err;
	const static struct adc_dt_spec d = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

	if (!device_is_ready(d.dev)) {
                        printk("ADC controller device not ready\n");
                        return NULL;
	}

	err = adc_channel_setup_dt(&d);
	if (err < 0) {
		printk("Could not setup adc channel (%d)\n", err);
		return NULL;
	}

	return &d;
}

int16_t lmt86_get_temperature(const struct adc_dt_spec *d)
{
	int err;
	int16_t buf;
	int32_t val_mv;
	float t;
	struct adc_sequence sequence = {
                .buffer = &buf,
                /* buffer size in bytes, not number of samples */
                .buffer_size = sizeof(buf),
        };

	printk("ADC reading:\n");
        (void)adc_sequence_init_dt(d, &sequence);

        err = adc_read(d->dev, &sequence);
        if (err < 0) {
               printk("Could not read (%d)\n", err);
        } else {
               printk("%"PRId16, buf);
        }

	/* conversion to mV may not be supported, skip if not */
	val_mv = buf;
	err = adc_raw_to_millivolts_dt(d,
				      &val_mv);
	if (err < 0) {
		printk(" (value in mV not available)\n");
	} else {
		printk(" = %"PRId32" mV\n", val_mv);
	}

	/* translation from mV to temperature */
	t = 5.0 + (( val_mv - 2047 ) / - 10.8 );
	t = t * 10;

	return (int) t;
}


