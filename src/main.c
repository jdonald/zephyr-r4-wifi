/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arduino Uno R4 WiFi — "Firmware DevX" demo.
 *
 * On each reset:
 *   1. Blinks the built-in LED (D13) in the "Shave and a Haircut, Two Bits"
 *      rhythm.
 *   2. Loops forever, scrolling the words FIRM / WARE / DEV / X downward
 *      across the 12×8 LED matrix, with a large X held for 3 s before
 *      cycling back to FIRM.
 *
 * LED matrix hardware: IS31FL3741A on the Arduino Uno R4 WiFi (I2C).
 * Zephyr driver: CONFIG_IS31FL3741 (drivers/led/is31fl3741.c).
 * Device node label: "is31fl3741" — adjust if your board DTS differs.
 *
 * LED index mapping assumed: idx = row * MATRIX_COLS + col  (row-major).
 * If the matrix renders mirrored or transposed, adjust matrix_led_idx().
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>

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

	for (int i = 0; i < (int)(sizeof(gaps) / sizeof(gaps[0])); i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(KNOCK_MS);
		gpio_pin_set_dt(&led, 0);
		if (gaps[i] > 0) {
			k_msleep(gaps[i]);
		}
	}
}

/* -----------------------------------------------------------------------
 * 12×8 LED matrix — IS31FL3741A
 * --------------------------------------------------------------------- */

#define MATRIX_ROWS 8
#define MATRIX_COLS 12

static const struct device *matrix_dev;

static inline int matrix_led_idx(int row, int col)
{
	return row * MATRIX_COLS + col;
}

static void matrix_clear(void)
{
	for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; i++) {
		led_set_brightness(matrix_dev, i, 0);
	}
}

static void matrix_set(int row, int col, bool on)
{
	if (row < 0 || row >= MATRIX_ROWS || col < 0 || col >= MATRIX_COLS) {
		return;
	}
	led_set_brightness(matrix_dev, matrix_led_idx(row, col), on ? 100 : 0);
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
	/* X */ { 0b101, 0b101, 0b010, 0b101, 0b101 },
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
#define CHAR_X 9

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

/* Three scrolling words (X is handled separately as a large glyph). */
static const struct word scroll_words[] = {
	{ chars_FIRM, 4 },
	{ chars_WARE, 4 },
	{ chars_DEV,  3 },
};

/* -----------------------------------------------------------------------
 * Rendering helpers
 * --------------------------------------------------------------------- */

/*
 * Draw a word on the matrix, with the top of the 5-row glyph at screen
 * row `y_top`. Clipped to the visible area.  Characters are 3 px wide
 * with no inter-character gap, packed tightly and centred horizontally.
 *
 *   4-char word (FIRM/WARE): total width = 12 px — perfect fit, x_start = 0
 *   3-char word (DEV):       total width =  9 px — x_start = 2 (centred)
 */
static void render_word(const struct word *w, int y_top)
{
	int total_width = w->len * 3;
	int x_start     = (MATRIX_COLS - total_width) / 2;

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
 * Scroll a word downward across the full display (enters from top,
 * exits at bottom).  Each step shifts one row; frame delay gives a
 * comfortable reading speed.
 */
static void scroll_word(const struct word *w)
{
#define SCROLL_FRAME_MS 80   /* ~12 fps */
#define FONT_HEIGHT     5

	/*
	 * y_top goes from -(FONT_HEIGHT-1) (glyph mostly above display,
	 * first row just peeking in) to MATRIX_ROWS (glyph fully below).
	 */
	for (int y = -(FONT_HEIGHT - 1); y <= MATRIX_ROWS; y++) {
		render_word(w, y);
		k_msleep(SCROLL_FRAME_MS);
	}
}

/*
 * Large diagonal X filling the full 12×8 matrix, held for a few seconds.
 *
 * Bit layout: bit 0 = leftmost column (col 0), bit 11 = rightmost (col 11).
 *
 *   Row 0:  cols  0, 11   →  0x801
 *   Row 1:  cols  1, 10   →  0x402
 *   Row 2:  cols  2,  9   →  0x204
 *   Row 3:  cols 3,4, 7,8 →  0x198
 *   Row 4:  cols 3,4, 7,8 →  0x198
 *   Row 5:  cols  2,  9   →  0x204
 *   Row 6:  cols  1, 10   →  0x402
 *   Row 7:  cols  0, 11   →  0x801
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

	/* Step 2: initialise the LED matrix. */
	matrix_dev = DEVICE_DT_GET(DT_NODELABEL(is31fl3741));
	if (!device_is_ready(matrix_dev)) {
		/* No matrix available — nothing more to do. */
		return 0;
	}

	matrix_clear();

	/* Step 3: scroll FIRM / WARE / DEV downward, then hold large X. */
	while (1) {
		for (int i = 0; i < (int)ARRAY_SIZE(scroll_words); i++) {
			scroll_word(&scroll_words[i]);
		}
		display_large_x();
	}

	return 0;
}
