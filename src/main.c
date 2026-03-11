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
 *
 * LED matrix hardware
 * -------------------
 * IS31FL3741A I2C LED controller on the Arduino Uno R4 WiFi.
 * Zephyr has no IS31FL3741A driver, so we talk to it directly via the
 * raw I2C API on the zephyr_i2c bus (iic0, defined in the WiFi board
 * overlay with SCL=P4_0, SDA=P4_1).
 *
 * IS31FL3741A register model (brief)
 * ------------------------------------
 *  Write 0xC5 to register 0xFE  →  unlock the command register
 *  Write page number to 0xFD    →  select active register page
 *
 *  Page 0  (0x00): PWM bytes for LEDs  0–179  (SW1–SW4, full; SW5 partial)
 *  Page 1  (0x01): PWM bytes for LEDs 180–350 (SW5 tail + SW6–SW9)
 *  Page 2  (0x02): Scaling bytes, same layout as page 0
 *  Page 3  (0x03): Scaling bytes, same layout as page 1
 *  Page 4  (0x04): Function registers (config, global current, reset …)
 *
 * Within pages 0/1 each LED is addressed by its (SW, CS) pair:
 *   offset = (sw - 1) * 39 + (cs - 1)        (sw/cs are 1-indexed)
 *   page   = (offset < 180) ? 0 : 1
 *   reg    = offset % 180                     (offset - 180 for page 1)
 *
 * LED matrix wiring assumption
 * ----------------------------
 * We assume row maps to SW (row 0 → SW1) and col maps to CS (col 0 → CS1).
 * All 12 columns use CS1–CS12, which are well below the CS25 page-0/1
 * boundary, so rows 0–4 land entirely in page 0 and rows 5–7 in page 1.
 * If the physical matrix appears mirrored or transposed, adjust
 * matrix_led_offset() below.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <string.h>

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
 * IS31FL3741A — raw I2C access
 *
 * The WiFi board overlay defines:  zephyr_i2c: &iic0 {}
 * so DT_NODELABEL(zephyr_i2c) resolves to the iic0 controller node.
 * --------------------------------------------------------------------- */

#define IS31_I2C_NODE  DT_NODELABEL(iic0)
#define IS31_ADDR      0x30

/* Register/page constants */
#define IS31_REG_UNLOCK    0xFE
#define IS31_UNLOCK_KEY    0xC5
#define IS31_REG_CMD       0xFD
#define IS31_PAGE_PWM0     0x00
#define IS31_PAGE_PWM1     0x01
#define IS31_PAGE_SCALE0   0x02
#define IS31_PAGE_SCALE1   0x03
#define IS31_PAGE_FUNC     0x04
/* Function-page register offsets */
#define IS31_FUNC_CONFIG   0x00   /* bit 0 = software enable */
#define IS31_FUNC_GCUR     0x01   /* global current control  */
#define IS31_FUNC_RESET    0x3F   /* write 0xAE to soft-reset */

#define MATRIX_ROWS 8
#define MATRIX_COLS 12

static const struct device *i2c_dev;
static bool matrix_ok;

/* Compute the linear channel offset for (row, col).
 * row 0-7 → SW 1-8, col 0-11 → CS 1-12. */
static inline int matrix_led_offset(int row, int col)
{
	return row * 39 + col;   /* (sw-1)*39 + (cs-1) */
}

/* Select IS31FL3741A register page.  Must unlock command reg first. */
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

/* Write one byte to a register on the currently-selected page. */
static int is31_write_reg(uint8_t reg, uint8_t val)
{
	uint8_t buf[] = { reg, val };

	return i2c_write(i2c_dev, buf, sizeof(buf), IS31_ADDR);
}

/* Bulk write: send reg-address byte followed by `len` data bytes.
 * The IS31FL3741A auto-increments its register pointer. */
static int is31_burst_write(uint8_t start_reg,
			    const uint8_t *data, uint16_t len)
{
	/* We need to prepend the register address in a single I2C
	 * transaction; build a small header and use i2c_transfer. */
	struct i2c_msg msgs[2];

	msgs[0].buf   = &start_reg;
	msgs[0].len   = 1;
	msgs[0].flags = I2C_MSG_WRITE;

	msgs[1].buf   = (uint8_t *)data;
	msgs[1].len   = len;
	msgs[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer(i2c_dev, msgs, 2, IS31_ADDR);
}

/* Initialise the IS31FL3741A.  Returns true on success. */
static bool is31_init(void)
{
	if (!device_is_ready(i2c_dev)) {
		return false;
	}

	/* Soft-reset.  Write 0xAE to function reg 0x3F. */
	is31_select_page(IS31_PAGE_FUNC);
	if (is31_write_reg(IS31_FUNC_RESET, 0xAE) != 0) {
		/* No ACK → chip not reachable on this bus. */
		return false;
	}
	k_msleep(10);

	/* Global current ~25 %, enable chip. */
	is31_select_page(IS31_PAGE_FUNC);
	is31_write_reg(IS31_FUNC_GCUR, 0x20);
	is31_write_reg(IS31_FUNC_CONFIG, 0x01);

	/* Set scaling to maximum for all channels we use.
	 * Rows 0–4 (SW1–SW5) are in scale page 2 (same offsets as PWM page 0).
	 * Rows 5–7 (SW6–SW8) are in scale page 3 (same offsets as PWM page 1).
	 * It is safe (and simpler) to fill the entire scaling pages with 0xFF. */
	static const uint8_t ff[180];   /* file-scope zero, overwritten below */
	uint8_t sc[180];

	memset(sc, 0xFF, sizeof(sc));

	is31_select_page(IS31_PAGE_SCALE0);
	is31_burst_write(0x00, sc, 180);

	is31_select_page(IS31_PAGE_SCALE1);
	is31_burst_write(0x00, sc, 171); /* page 1 has 351-180 = 171 channels */

	(void)ff; /* suppress unused-variable warning */
	return true;
}

/* -----------------------------------------------------------------------
 * LED matrix drawing primitives
 * --------------------------------------------------------------------- */

static void matrix_clear(void)
{
	if (!matrix_ok) {
		return;
	}

	/* Fill PWM page 0 (regs 0x00–0xB3) with zeros. */
	uint8_t zeros[180];

	memset(zeros, 0, sizeof(zeros));

	is31_select_page(IS31_PAGE_PWM0);
	is31_burst_write(0x00, zeros, 180);

	/* Fill used portion of PWM page 1.
	 * Highest offset used: row 7, col 11 → 7*39+11 = 284,
	 * page-1 register = 284-180 = 104 (0x68). */
	is31_select_page(IS31_PAGE_PWM1);
	is31_burst_write(0x00, zeros, 105); /* regs 0x00–0x68 */
}

static void matrix_set(int row, int col, bool on)
{
	if (!matrix_ok) {
		return;
	}
	if (row < 0 || row >= MATRIX_ROWS || col < 0 || col >= MATRIX_COLS) {
		return;
	}

	int offset = matrix_led_offset(row, col);
	uint8_t page = (offset < 180) ? IS31_PAGE_PWM0 : IS31_PAGE_PWM1;
	uint8_t reg  = (uint8_t)((offset < 180) ? offset : offset - 180);

	is31_select_page(page);
	is31_write_reg(reg, on ? 0xFF : 0x00);
}

/* -----------------------------------------------------------------------
 * 3×5 pixel font (MSB = left pixel of each row)
 * --------------------------------------------------------------------- */

static const uint8_t font3x5[][5] = {
	/* F */ { 0b111, 0b100, 0b110, 0b100, 0b100 },
	/* I */ { 0b111, 0b010, 0b010, 0b010, 0b111 },
	/* R */ { 0b110, 0b101, 0b110, 0b101, 0b101 },
	/* M */ { 0b101, 0b111, 0b101, 0b101, 0b101 },
	/* W */ { 0b101, 0b101, 0b111, 0b111, 0b010 },
	/* A */ { 0b010, 0b101, 0b111, 0b101, 0b101 },
	/* E */ { 0b111, 0b100, 0b110, 0b100, 0b111 },
	/* D */ { 0b110, 0b101, 0b101, 0b101, 0b110 },
	/* V */ { 0b101, 0b101, 0b101, 0b010, 0b010 },
};

#define CHAR_F 0
#define CHAR_I 1
#define CHAR_R 2
#define CHAR_M 3
#define CHAR_W 4
#define CHAR_A 5
#define CHAR_E 6
#define CHAR_D 7
#define CHAR_V 8

/* -----------------------------------------------------------------------
 * Words: arrays of character indices
 * --------------------------------------------------------------------- */

struct word {
	const int *chars;
	int len;
};

static const int chars_FIRM[] = { CHAR_F, CHAR_I, CHAR_R, CHAR_M };
static const int chars_WARE[] = { CHAR_W, CHAR_A, CHAR_R, CHAR_E };
static const int chars_DEV[]  = { CHAR_D, CHAR_E, CHAR_V };

static const struct word scroll_words[] = {
	{ chars_FIRM, 4 },
	{ chars_WARE, 4 },
	{ chars_DEV,  3 },
};

/* -----------------------------------------------------------------------
 * Rendering helpers
 * --------------------------------------------------------------------- */

/*
 * Draw a word with the top of its 5-row glyph at screen row `y_top`.
 * Characters are 3 px wide, no inter-character gap, centred horizontally.
 *   4-char word (FIRM/WARE): 12 px → x_start = 0
 *   3-char word (DEV):        9 px → x_start = 2 (1 left, 2 right margin)
 */
static void render_word(const struct word *w, int y_top)
{
	int x_start = (MATRIX_COLS - w->len * 3) / 2;

	matrix_clear();

	for (int ci = 0; ci < w->len; ci++) {
		const uint8_t *glyph = font3x5[w->chars[ci]];
		int x = x_start + ci * 3;

		for (int gy = 0; gy < 5; gy++) {
			int row = y_top + gy;

			if (row < 0 || row >= MATRIX_ROWS) {
				continue;
			}
			for (int gx = 0; gx < 3; gx++) {
				bool on = (glyph[gy] >> (2 - gx)) & 1;

				matrix_set(row, x + gx, on);
			}
		}
	}
}

/*
 * Scroll a word downward: enters at the top, exits at the bottom.
 */
static void scroll_word(const struct word *w)
{
#define SCROLL_FRAME_MS  80
#define FONT_HEIGHT       5

	for (int y = -(FONT_HEIGHT - 1); y <= MATRIX_ROWS; y++) {
		render_word(w, y);
		k_msleep(SCROLL_FRAME_MS);
	}
}

/*
 * Display a large diagonal X filling the full 12×8 matrix for 3 seconds.
 *
 * Bit layout: bit 0 = col 0 (left), bit 11 = col 11 (right).
 *   Row 0:  cols  0, 11   → 0x801
 *   Row 1:  cols  1, 10   → 0x402
 *   Row 2:  cols  2,  9   → 0x204
 *   Row 3:  cols 3,4, 7,8 → 0x198
 *   Row 4:  cols 3,4, 7,8 → 0x198
 *   Row 5:  cols  2,  9   → 0x204
 *   Row 6:  cols  1, 10   → 0x402
 *   Row 7:  cols  0, 11   → 0x801
 */
static void display_large_x(void)
{
	static const uint16_t large_x[MATRIX_ROWS] = {
		0x801, 0x402, 0x204, 0x198,
		0x198, 0x204, 0x402, 0x801,
	};

	matrix_clear();
	for (int row = 0; row < MATRIX_ROWS; row++) {
		for (int col = 0; col < MATRIX_COLS; col++) {
			matrix_set(row, col, (large_x[row] >> col) & 1);
		}
	}
	k_msleep(3000);
}

/* -----------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */

int main(void)
{
	/* Step 1: classic rhythm on D13. */
	shave_and_a_haircut();

	/* Step 2: get the I2C bus and initialise the LED matrix. */
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(iic0));
	matrix_ok = is31_init();

	/* Step 3: scroll FIRM / WARE / DEV, then hold large X — forever. */
	while (1) {
		for (int i = 0; i < (int)ARRAY_SIZE(scroll_words); i++) {
			scroll_word(&scroll_words[i]);
		}
		display_large_x();
	}

	return 0;
}
