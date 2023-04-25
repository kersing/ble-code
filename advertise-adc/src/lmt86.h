/*
 *
 * LMT86 interface
 *
 * Copyright (c) 2023 The-Box Development
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef __LMT86__H
#define __LMT86__H

const struct adc_dt_spec *get_device_lmt86();
int16_t lmt86_get_temperature(const struct adc_dt_spec *d);
#endif
