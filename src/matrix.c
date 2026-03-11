/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * IS31FL3741A raw-I2C driver for the Arduino Uno R4 WiFi LED matrix.
 *
 * Register model summary:
 *   Write 0xC5 → reg 0xFE  : unlock command register
 *   Write <page> → reg 0xFD: select active register page
 *   Page 0 (PWM, LEDs   0-179), Page 1 (PWM, LEDs 180-350)
 *   Page 2 (Scale, 0-179),      Page 3 (Scale, 180-350)
 *   Page 4: Function registers (config, global current, reset)
 */

#include "matrix.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <string.h>

#define IS31_ADDR           0x30

#define IS31_REG_UNLOCK     0xFE
#define IS31_UNLOCK_KEY     0xC5
#define IS31_REG_CMD        0xFD

#define IS31_PAGE_PWM0      0x00
#define IS31_PAGE_PWM1      0x01
#define IS31_PAGE_SCALE0    0x02
#define IS31_PAGE_SCALE1    0x03
#define IS31_PAGE_FUNC      0x04

#define IS31_FUNC_CONFIG    0x00   /* bit 0 = software enable */
#define IS31_FUNC_GCUR      0x01   /* global current control  */
#define IS31_FUNC_RESET     0x3F   /* write 0xAE to soft-reset */

static const struct device *i2c_dev;
static bool matrix_ok;

static int is31_select_page(uint8_t page)
{
	uint8_t unlock[] = { IS31_REG_UNLOCK, IS31_UNLOCK_KEY };
	int r = i2c_write(i2c_dev, unlock, sizeof(unlock), IS31_ADDR);

	if (r) {
		return r;
	}
	uint8_t cmd[] = { IS31_REG_CMD, page };

	return i2c_write(i2c_dev, cmd, sizeof(cmd), IS31_ADDR);
}

static int is31_write_reg(uint8_t reg, uint8_t val)
{
	uint8_t buf[] = { reg, val };

	return i2c_write(i2c_dev, buf, sizeof(buf), IS31_ADDR);
}

static int is31_burst_write(uint8_t start_reg,
			    const uint8_t *data, uint16_t len)
{
	struct i2c_msg msgs[2];

	msgs[0].buf   = &start_reg;
	msgs[0].len   = 1;
	msgs[0].flags = I2C_MSG_WRITE;

	msgs[1].buf   = (uint8_t *)data;
	msgs[1].len   = len;
	msgs[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer(i2c_dev, msgs, 2, IS31_ADDR);
}

bool matrix_init(void)
{
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(iic0));
	if (!device_is_ready(i2c_dev)) {
		return false;
	}

	/* Soft-reset. */
	is31_select_page(IS31_PAGE_FUNC);
	if (is31_write_reg(IS31_FUNC_RESET, 0xAE) != 0) {
		return false;
	}
	k_msleep(10);

	/* Global current ~25 %, enable chip. */
	is31_select_page(IS31_PAGE_FUNC);
	is31_write_reg(IS31_FUNC_GCUR, 0x20);
	is31_write_reg(IS31_FUNC_CONFIG, 0x01);

	/* Set all scaling registers to maximum. */
	uint8_t sc[180];

	memset(sc, 0xFF, sizeof(sc));

	is31_select_page(IS31_PAGE_SCALE0);
	is31_burst_write(0x00, sc, 180);

	is31_select_page(IS31_PAGE_SCALE1);
	is31_burst_write(0x00, sc, 171); /* 351 - 180 = 171 channels */

	matrix_ok = true;
	return true;
}

void matrix_clear(void)
{
	if (!matrix_ok) {
		return;
	}

	uint8_t zeros[180];

	memset(zeros, 0, sizeof(zeros));

	is31_select_page(IS31_PAGE_PWM0);
	is31_burst_write(0x00, zeros, 180);

	/* Highest used offset: row 7, col 11 → 7*39+11 = 284, reg = 104. */
	is31_select_page(IS31_PAGE_PWM1);
	is31_burst_write(0x00, zeros, 105);
}

void matrix_set(int row, int col, bool on)
{
	if (!matrix_ok) {
		return;
	}
	if (row < 0 || row >= MATRIX_ROWS || col < 0 || col >= MATRIX_COLS) {
		return;
	}

	int offset    = matrix_led_offset(row, col);
	uint8_t page  = (offset < 180) ? IS31_PAGE_PWM0 : IS31_PAGE_PWM1;
	uint8_t reg   = (uint8_t)((offset < 180) ? offset : offset - 180);

	is31_select_page(page);
	is31_write_reg(reg, on ? 0xFF : 0x00);
}
