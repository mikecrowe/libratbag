/*
 * Copyright © 2020 Marian Beermann
 * Copyright © 2020 Mike Crowe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define SINOWEALTH7_REPORT_ID_CONFIG 0x4
#define SINOWEALTH7_CONFIG_SIZE 0x9a

struct sinowealth7_config_report {
    uint8_t report_id; /* SINOWEALTH7_REPORT_ID_CONFIG */
    uint8_t unknown1[0x5f];
    uint8_t led_brightness;
    uint8_t unknown2[0x9a-0x61];
} __attribute__((packed));

_Static_assert(sizeof(struct sinowealth7_config_report) == SINOWEALTH7_CONFIG_SIZE, "Invalid size");

struct sinowealth7_data {
	/* this is kinda unnecessary at this time, but all the other drivers do it too ;) */
	struct sinowealth7_config_report config;
};

static int
sinowealth7_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth7_data *drv_data = device->drv_data;
	struct sinowealth7_config_report *config = &drv_data->config;
//	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	unsigned int hz = 1000; /* TODO */
	int rc;

	rc = ratbag_hidraw_get_feature_report(device, SINOWEALTH7_REPORT_ID_CONFIG,
					      (uint8_t*) config, SINOWEALTH7_CONFIG_SIZE);
	/* The GET_FEATURE report length has to be 520, but the actual data returned is less */
	if (rc != SINOWEALTH7_CONFIG_SIZE) {
		log_error(device->ratbag, "Could not read device configuration: %d\n", rc);
		return -1;
	}

	log_buffer(device->ratbag, RATBAG_LOG_PRIORITY_INFO, "CONFIG\n", (uint8_t *)config, SINOWEALTH7_CONFIG_SIZE);

	/* Body lighting */
	led = ratbag_profile_get_led(profile, 0);
	led->mode = RATBAG_LED_ON;
	led->brightness = config->led_brightness;
	ratbag_led_unref(led);

	/* TODO */
	ratbag_profile_set_report_rate_list(profile, &hz, 1);
	ratbag_profile_set_report_rate(profile, hz);

#if 0
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (config->config & SINOWEALTH_XY_INDEPENDENT) {
			resolution->dpi_x = sinowealth_raw_to_dpi(config->dpi[resolution->index * 2]);
			resolution->dpi_y = sinowealth_raw_to_dpi(config->dpi[resolution->index * 2 + 1]);
		} else {
			resolution->dpi_x = sinowealth_raw_to_dpi(config->dpi[resolution->index]);
			resolution->dpi_y = resolution->dpi_x;
		}
		if (config->dpi_enabled & (1<<resolution->index)) {
			/* DPI step is disabled, fake it by setting DPI to 0 */
			resolution->dpi_x = 0;
			resolution->dpi_y = 0;
		}
		resolution->is_active = resolution->index == config->active_dpi - 1;
		resolution->is_default = resolution->is_active;
	}
#endif

	profile->is_active = true;

	return 0;
}

#define SINOWEALTH7_DPI_MAX 1200
#define SINOWEALTH7_DPI_MIN 1200
#define SINOWEALTH7_DPI_STEP 200

static void
sinowealth7_init_profile(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

//	int num_dpis = (SINOWEALTH7_DPI_MAX - SINOWEALTH7_DPI_MIN) / SINOWEALTH_DPI_STEP + 2;
	const int num_dpis = 2;
	unsigned int dpis[num_dpis];

	ratbag_device_init_profiles(device,
				    1, // num_profiles
				    1, // num_dpis
				    0, // num_buttons
				    1); // num_leds

	profile = ratbag_device_get_profile(device, 0);

	/* Generate DPI list */
	/* PC software has:
	   { 500, 750, 1000, 1200, 1600, 2000, 2400, 3000, 3200, 3500, 4000, 4500, 5000, 5500, 6000, 7200 }
	*/

	dpis[0] = 0; /* 0 DPI = disabled */
	dpis[1] = 1200;
//	for (int i = 1; i < num_dpis; i++) {
//		dpis[i] = SINOWEALTH_DPI_MIN + (i - 1) * SINOWEALTH_DPI_STEP;
//	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		ratbag_resolution_set_dpi_list(resolution, dpis, num_dpis);
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
	}

	/* Set up LED capabilities */
	led = ratbag_profile_get_led(profile, 0);
	led->type = RATBAG_LED_TYPE_SIDE;
	led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME; // todo
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_unref(led);
	ratbag_profile_unref(profile);
}

static int
sinowealth7_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, SINOWEALTH7_REPORT_ID_CONFIG);
}

static int
sinowealth7_probe(struct ratbag_device *device)
{
	int rc;
	struct sinowealth7_data *drv_data = 0;
	struct ratbag_profile *profile = 0;

	rc = ratbag_find_hidraw(device, sinowealth7_test_hidraw);
	if (rc)
		goto err;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	sinowealth7_init_profile(device);

	profile = ratbag_device_get_profile(device, 0);
	rc = sinowealth7_read_profile(profile);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, 0);
	return rc;
}

static int
sinowealth7_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile = ratbag_device_get_profile(device, 0);
	struct sinowealth7_data *drv_data = device->drv_data;
	struct sinowealth7_config_report *config = &drv_data->config;
	struct ratbag_led *led;
	int rc;

	led = ratbag_profile_get_led(profile, 0);
	switch(led->mode) {
	case RATBAG_LED_OFF:
	    config->led_brightness = 0x2;
	    break;
	case RATBAG_LED_ON:
	    config->led_brightness = 0xa2;
	    break;
	case RATBAG_LED_CYCLE:
	case RATBAG_LED_BREATHING:
	    break;
	}
	ratbag_led_unref(led);

	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH7_REPORT_ID_CONFIG,
					      (uint8_t*) config, SINOWEALTH7_CONFIG_SIZE);
	if (rc != SINOWEALTH7_CONFIG_SIZE) {
		log_error(device->ratbag, "Error while writing config: %d\n", rc);
		ratbag_profile_unref(profile);
		return -1;
	}

	ratbag_profile_unref(profile);
	return 0;
}

static void
sinowealth7_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver sinowealth7_driver = {
	.name = "Sinowealth7",
	.id = "sinowealth7",
	.probe = sinowealth7_probe,
	.remove = sinowealth7_remove,
	.commit = sinowealth7_commit
};
