/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Font data, word definitions, and LED-matrix rendering.
 */

#include "render.h"
#include "matrix.h"

#include <zephyr/kernel.h>
#include <stdbool.h>

#define SCROLL_FRAME_MS 80

/* 3×5 font — row order top-to-bottom, MSB = leftmost pixel. */
const uint8_t font3x5[][FONT_HEIGHT] = {
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

static const int chars_FIRM[] = { CHAR_F, CHAR_I, CHAR_R, CHAR_M };
static const int chars_WARE[] = { CHAR_W, CHAR_A, CHAR_R, CHAR_E };
static const int chars_DEV[]  = { CHAR_D, CHAR_E, CHAR_V };

const struct word scroll_words[] = {
	{ chars_FIRM, 4 },
	{ chars_WARE, 4 },
	{ chars_DEV,  3 },
};

const int num_scroll_words = (int)(sizeof(scroll_words) / sizeof(scroll_words[0]));

void render_word(const struct word *w, int y_top)
{
	int x_start = (MATRIX_COLS - w->len * FONT_WIDTH) / 2;

	matrix_clear();

	for (int ci = 0; ci < w->len; ci++) {
		const uint8_t *glyph = font3x5[w->chars[ci]];
		int x = x_start + ci * FONT_WIDTH;

		for (int gy = 0; gy < FONT_HEIGHT; gy++) {
			int row = y_top + gy;

			if (row < 0 || row >= MATRIX_ROWS) {
				continue;
			}
			for (int gx = 0; gx < FONT_WIDTH; gx++) {
				bool on = (glyph[gy] >> (2 - gx)) & 1;

				matrix_set(row, x + gx, on);
			}
		}
	}
}

void scroll_word(const struct word *w)
{
	for (int y = -(FONT_HEIGHT - 1); y <= MATRIX_ROWS; y++) {
		render_word(w, y);
		k_msleep(SCROLL_FRAME_MS);
	}
}

void display_large_x_frame(void)
{
	/*
	 * Diagonal X pattern for an 8×12 display.
	 * Bit N = col N (bit 0 = col 0).
	 *   Row 0: cols  0, 11  → 0x801
	 *   Row 1: cols  1, 10  → 0x402
	 *   Row 2: cols  2,  9  → 0x204
	 *   Row 3: cols 3,4,7,8 → 0x198
	 *   Row 4: cols 3,4,7,8 → 0x198
	 *   Row 5: cols  2,  9  → 0x204
	 *   Row 6: cols  1, 10  → 0x402
	 *   Row 7: cols  0, 11  → 0x801
	 */
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
}

void display_large_x(void)
{
	display_large_x_frame();
	k_msleep(3000);
}
