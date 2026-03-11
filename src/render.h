/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rendering layer: 3×5 font, word scrolling, and the large-X animation.
 * Depends only on matrix_clear() / matrix_set() — not on the I2C driver —
 * so render.c can be linked against a stub matrix for unit testing.
 */

#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "matrix.h"

#define FONT_WIDTH  3
#define FONT_HEIGHT 5

/* Character indices into font3x5[]. */
#define CHAR_F 0
#define CHAR_I 1
#define CHAR_R 2
#define CHAR_M 3
#define CHAR_W 4
#define CHAR_A 5
#define CHAR_E 6
#define CHAR_D 7
#define CHAR_V 8

/* 3×5 pixel font; font3x5[c][row] — MSB = left pixel. */
extern const uint8_t font3x5[][FONT_HEIGHT];

struct word {
	const int *chars;
	int len;
};

extern const struct word scroll_words[];
extern const int num_scroll_words;

/*
 * Draw `w` onto the matrix with the top of its 5-row glyph at screen row
 * `y_top`.  Rows outside [0, MATRIX_ROWS) are silently clipped.
 * Calls matrix_clear() then matrix_set() as required.
 */
void render_word(const struct word *w, int y_top);

/*
 * Scroll `w` downward across the full display (enters at top, exits at
 * bottom), pausing SCROLL_FRAME_MS between frames.
 */
void scroll_word(const struct word *w);

/*
 * Draw the large diagonal X pattern — no delay.
 * Call this directly from tests to avoid the 3-second k_msleep.
 */
void display_large_x_frame(void);

/* display_large_x_frame() followed by a 3-second pause. */
void display_large_x(void);

#endif /* RENDER_H */
