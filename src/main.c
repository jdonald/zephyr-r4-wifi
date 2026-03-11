/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arduino Uno R4 WiFi — "Firmware DevX" demo.
 *
 * On each reset:
 *   1. Blinks the built-in LED (D13) in the "Shave and a Haircut, Two Bits"
 *      rhythm.
 *   2. Loops forever, scrolling FIRM / WARE / DEV downward across the 12×8
 *      LED matrix, then holds a large diagonal X for 3 s, then repeats.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "matrix.h"
#include "render.h"

/* -----------------------------------------------------------------------
 * D13 LED — "Shave and a Haircut" rhythm
 * --------------------------------------------------------------------- */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define KNOCK_MS 120
#define EIGHTH   250
#define QUARTER  500
#define HALF     1000

static const int gaps[] = {
	QUARTER - KNOCK_MS,        /* "Shave"  */
	EIGHTH  - KNOCK_MS,        /* "and"    */
	EIGHTH  - KNOCK_MS,        /* "a"      */
	QUARTER - KNOCK_MS,        /* "hair"   */
	QUARTER + HALF - KNOCK_MS, /* "cut" — big pause */
	QUARTER - KNOCK_MS,        /* "two"    */
	0,                         /* "bits"   */
};

static void shave_and_a_haircut(void)
{
	if (!gpio_is_ready_dt(&led)) {
		return;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	k_msleep(QUARTER);

	for (int i = 0; i < (int)ARRAY_SIZE(gaps); i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(KNOCK_MS);
		gpio_pin_set_dt(&led, 0);
		if (gaps[i] > 0) {
			k_msleep(gaps[i]);
		}
	}
}

/* -----------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */

int main(void)
{
	/* Step 1: classic rhythm on D13. */
	shave_and_a_haircut();

	/* Step 2: initialise the IS31FL3741A LED matrix over I2C. */
	matrix_init();

	/* Step 3: scroll FIRM / WARE / DEV, then hold large X — forever. */
	while (1) {
		for (int i = 0; i < num_scroll_words; i++) {
			scroll_word(&scroll_words[i]);
		}
		display_large_x();
	}

	return 0;
}
