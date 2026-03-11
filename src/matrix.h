/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * IS31FL3741A LED matrix driver interface.
 * Hardware: IS31FL3741A on the Arduino Uno R4 WiFi, reachable via iic0
 * (SCL=P4_0, SDA=P4_1, I2C address 0x30).
 */

#ifndef MATRIX_H
#define MATRIX_H

#include <stdbool.h>

#define MATRIX_ROWS 8
#define MATRIX_COLS 12

/*
 * LED offset formula: offset = (sw-1)*39 + (cs-1) = row*39 + col.
 * page 0 covers offsets 0-179, page 1 covers 180-350.
 * Exposed as an inline so tests can verify the math without I2C.
 */
static inline int matrix_led_offset(int row, int col)
{
	return row * 39 + col;
}

/*
 * Initialise the IS31FL3741A.  Must be called before matrix_clear/matrix_set.
 * Returns true on success (chip found and configured).
 */
bool matrix_init(void);

/* Fill all PWM registers with 0 (blank display). */
void matrix_clear(void);

/* Set a single LED on or off.  Out-of-range coordinates are silently ignored. */
void matrix_set(int row, int col, bool on);

#endif /* MATRIX_H */
