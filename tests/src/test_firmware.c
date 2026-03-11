/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the firmware rendering layer.
 *
 * render.c is compiled into this test binary but matrix.c is NOT.
 * Instead we provide stub implementations of matrix_clear() and
 * matrix_set() that write into a software framebuffer so we can
 * inspect the rendered output without any hardware.
 *
 * IS31FL3741A offset arithmetic is tested independently using the
 * inline matrix_led_offset() helper from matrix.h.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <stdbool.h>

/* Pull in declarations from our app's source tree (added to include path
 * by tests/CMakeLists.txt). */
#include "matrix.h"
#include "render.h"

/* -----------------------------------------------------------------------
 * Software framebuffer stubs
 * --------------------------------------------------------------------- */

static bool fb[MATRIX_ROWS][MATRIX_COLS];
static int g_clear_calls;
static int g_set_calls;

static void fb_reset(void)
{
	memset(fb, 0, sizeof(fb));
	g_clear_calls = 0;
	g_set_calls   = 0;
}

/* Stub: replaces matrix.c's matrix_clear(). */
void matrix_clear(void)
{
	memset(fb, 0, sizeof(fb));
	g_clear_calls++;
}

/* Stub: replaces matrix.c's matrix_set(). */
void matrix_set(int row, int col, bool on)
{
	if (row >= 0 && row < MATRIX_ROWS && col >= 0 && col < MATRIX_COLS) {
		fb[row][col] = on;
	}
	g_set_calls++;
}

/* -----------------------------------------------------------------------
 * Helper: render a word at y_top=0 and return immediately.
 * --------------------------------------------------------------------- */
static void render_at_zero(int word_idx)
{
	fb_reset();
	render_word(&scroll_words[word_idx], 0);
}

/* -----------------------------------------------------------------------
 * Suite: test_glyphs — verify individual character pixel patterns
 * --------------------------------------------------------------------- */

ZTEST_SUITE(test_glyphs, NULL, NULL, NULL, NULL, NULL);

/* F: 111 / 100 / 110 / 100 / 100  (MSB = col 0) */
ZTEST(test_glyphs, test_F)
{
	render_at_zero(0); /* FIRM, x_start=0, F is at col 0 */

	/* row 0: all 3 pixels on */
	zassert_true(fb[0][0], "F row0 col0");
	zassert_true(fb[0][1], "F row0 col1");
	zassert_true(fb[0][2], "F row0 col2");

	/* row 1: only left pixel */
	zassert_true(fb[1][0],  "F row1 col0");
	zassert_false(fb[1][1], "F row1 col1 should be off");
	zassert_false(fb[1][2], "F row1 col2 should be off");

	/* row 2: two left pixels */
	zassert_true(fb[2][0],  "F row2 col0");
	zassert_true(fb[2][1],  "F row2 col1");
	zassert_false(fb[2][2], "F row2 col2 should be off");

	/* row 3: only left pixel */
	zassert_true(fb[3][0],  "F row3 col0");
	zassert_false(fb[3][1], "F row3 col1 should be off");

	/* row 4: only left pixel */
	zassert_true(fb[4][0],  "F row4 col0");
	zassert_false(fb[4][1], "F row4 col1 should be off");
}

/* I: 111 / 010 / 010 / 010 / 111  at col 3 (second char of FIRM) */
ZTEST(test_glyphs, test_I)
{
	render_at_zero(0); /* FIRM */

	/* row 0: all three cols of I lit */
	zassert_true(fb[0][3], "I row0 col3");
	zassert_true(fb[0][4], "I row0 col4");
	zassert_true(fb[0][5], "I row0 col5");

	/* row 1: only centre */
	zassert_false(fb[1][3], "I row1 col3 should be off");
	zassert_true(fb[1][4],  "I row1 col4");
	zassert_false(fb[1][5], "I row1 col5 should be off");

	/* row 4: all three cols lit again */
	zassert_true(fb[4][3], "I row4 col3");
	zassert_true(fb[4][4], "I row4 col4");
	zassert_true(fb[4][5], "I row4 col5");
}

/* M: 101 / 111 / 101 / 101 / 101  at col 9 (fourth char of FIRM) */
ZTEST(test_glyphs, test_M)
{
	render_at_zero(0); /* FIRM */

	/* row 0: outer two pixels on, middle off */
	zassert_true(fb[0][9],   "M row0 col9");
	zassert_false(fb[0][10], "M row0 col10 should be off");
	zassert_true(fb[0][11],  "M row0 col11");

	/* row 1: all three on */
	zassert_true(fb[1][9],  "M row1 col9");
	zassert_true(fb[1][10], "M row1 col10");
	zassert_true(fb[1][11], "M row1 col11");
}

/* V: 101 / 101 / 101 / 010 / 010  at col 6 (third char of DEV) */
ZTEST(test_glyphs, test_V)
{
	/* DEV: word index 2, x_start = (12 - 9) / 2 = 1
	 * D at col 1, E at col 4, V at col 7. */
	fb_reset();
	render_word(&scroll_words[2], 0);

	/* V: col 7 = left, col 8 = mid, col 9 = right */
	/* row 0: outer two on */
	zassert_true(fb[0][7],  "V row0 col7");
	zassert_false(fb[0][8], "V row0 col8 should be off");
	zassert_true(fb[0][9],  "V row0 col9");

	/* row 3: only centre */
	zassert_false(fb[3][7], "V row3 col7 should be off");
	zassert_true(fb[3][8],  "V row3 col8");
	zassert_false(fb[3][9], "V row3 col9 should be off");

	/* row 4: only centre */
	zassert_false(fb[4][7], "V row4 col7 should be off");
	zassert_true(fb[4][8],  "V row4 col8");
}

/* -----------------------------------------------------------------------
 * Suite: test_rendering — layout, centering, clipping
 * --------------------------------------------------------------------- */

ZTEST_SUITE(test_rendering, NULL, NULL, NULL, NULL, NULL);

/* FIRM (4 chars × 3 px = 12) must fill the full display width.
 * Check that every column has at least one lit pixel across all glyph rows. */
ZTEST(test_rendering, test_firm_fullscreen)
{
	render_at_zero(0);

	for (int col = 0; col < MATRIX_COLS; col++) {
		bool any_on = false;

		for (int row = 0; row < FONT_HEIGHT; row++) {
			if (fb[row][col]) {
				any_on = true;
				break;
			}
		}
		zassert_true(any_on,
			"FIRM col %d has no lit pixels in rows 0-%d",
			col, FONT_HEIGHT - 1);
	}
}

/* DEV (3 chars × 3 px = 9) must be centred: x_start = (12-9)/2 = 1. */
ZTEST(test_rendering, test_dev_centering)
{
	fb_reset();
	render_word(&scroll_words[2], 0); /* DEV */

	/* col 0 must be off — left margin */
	zassert_false(fb[0][0], "DEV col 0 should be dark (left margin)");

	/* col 1 onward: D starts at col 1, row 0 pattern is 110 →
	 * col 1 and col 2 on, col 3 off. */
	zassert_true(fb[0][1], "DEV D row0 col1 should be on");
	zassert_true(fb[0][2], "DEV D row0 col2 should be on");
}

/* Render FIRM with y_top=-3: glyph rows 3 and 4 visible at display rows 0,1. */
ZTEST(test_rendering, test_scroll_clip_top)
{
	fb_reset();
	render_word(&scroll_words[0], -3); /* FIRM, top clipped by 3 rows */

	/* Rows 2-7 must be blank (glyph has only 5 rows, rows 3 and 4
	 * map to display rows 0 and 1). */
	for (int row = 2; row < MATRIX_ROWS; row++) {
		for (int col = 0; col < MATRIX_COLS; col++) {
			zassert_false(fb[row][col],
				"clipped-top: row %d col %d should be off",
				row, col);
		}
	}

	/* At least one pixel must be set in rows 0 or 1 (glyph rows 3/4
	 * of FIRM — F row3=100, I row3=010, R row3=101, M row3=101). */
	bool any_on = false;

	for (int col = 0; col < MATRIX_COLS; col++) {
		if (fb[0][col] || fb[1][col]) {
			any_on = true;
		}
	}
	zassert_true(any_on, "clipped-top: expected some pixels in rows 0-1");
}

/* Render FIRM with y_top=6: only glyph rows 0 and 1 appear (display rows 6,7). */
ZTEST(test_rendering, test_scroll_clip_bottom)
{
	fb_reset();
	render_word(&scroll_words[0], 6); /* FIRM, bottom clipped */

	/* Rows 0-5 must be blank. */
	for (int row = 0; row < 6; row++) {
		for (int col = 0; col < MATRIX_COLS; col++) {
			zassert_false(fb[row][col],
				"clipped-bottom: row %d col %d should be off",
				row, col);
		}
	}

	/* Rows 6-7 must have at least one lit pixel (FIRM row0 = all-on). */
	bool any_on = false;

	for (int col = 0; col < MATRIX_COLS; col++) {
		if (fb[6][col] || fb[7][col]) {
			any_on = true;
		}
	}
	zassert_true(any_on, "clipped-bottom: expected pixels in rows 6-7");
}

/* Display the large X and verify corners and centre. */
ZTEST(test_rendering, test_large_x_corners)
{
	fb_reset();
	display_large_x_frame();

	/* Corners must all be lit. */
	zassert_true(fb[0][0],  "X corner (0,0)");
	zassert_true(fb[0][11], "X corner (0,11)");
	zassert_true(fb[7][0],  "X corner (7,0)");
	zassert_true(fb[7][11], "X corner (7,11)");
}

ZTEST(test_rendering, test_large_x_center)
{
	fb_reset();
	display_large_x_frame();

	/* Rows 3 and 4 have the centre cross of the X.
	 * large_x[3] = large_x[4] = 0x198 = bits 3,4,7,8. */
	zassert_true(fb[3][3], "X centre row3 col3");
	zassert_true(fb[3][4], "X centre row3 col4");
	zassert_true(fb[3][7], "X centre row3 col7");
	zassert_true(fb[3][8], "X centre row3 col8");

	/* Columns 0 and 11 must be off in the middle rows. */
	zassert_false(fb[3][0],  "X row3 col0 should be off");
	zassert_false(fb[3][11], "X row3 col11 should be off");
	zassert_false(fb[4][0],  "X row4 col0 should be off");
	zassert_false(fb[4][11], "X row4 col11 should be off");
}

/* -----------------------------------------------------------------------
 * Suite: test_no_hang — verify render paths terminate with correct counts
 * --------------------------------------------------------------------- */

ZTEST_SUITE(test_no_hang, NULL, NULL, NULL, NULL, NULL);

/*
 * scroll_word loops y from -(FONT_HEIGHT-1) to MATRIX_ROWS inclusive.
 * That is from -4 to 8: (8 - (-4) + 1) = 13 frames.
 * Each frame calls render_word which calls matrix_clear() once.
 */
ZTEST(test_no_hang, test_scroll_terminates)
{
	fb_reset();
	scroll_word(&scroll_words[0]); /* FIRM */

	zassert_equal(g_clear_calls, 13,
		"scroll_word should produce exactly 13 frames, got %d",
		g_clear_calls);
}

/*
 * display_large_x_frame calls matrix_clear() once, then matrix_set()
 * for every cell (MATRIX_ROWS × MATRIX_COLS = 96 times).
 */
ZTEST(test_no_hang, test_display_x_terminates)
{
	fb_reset();
	display_large_x_frame();

	zassert_equal(g_clear_calls, 1,
		"display_large_x_frame should clear once, got %d",
		g_clear_calls);
	zassert_equal(g_set_calls, MATRIX_ROWS * MATRIX_COLS,
		"display_large_x_frame should call matrix_set %d times, got %d",
		MATRIX_ROWS * MATRIX_COLS, g_set_calls);
}

/* -----------------------------------------------------------------------
 * Suite: test_is31_offsets — pure arithmetic, no I2C required
 * --------------------------------------------------------------------- */

ZTEST_SUITE(test_is31_offsets, NULL, NULL, NULL, NULL, NULL);

ZTEST(test_is31_offsets, test_offset_first_led)
{
	int off = matrix_led_offset(0, 0);

	zassert_equal(off, 0, "LED(0,0) offset should be 0, got %d", off);
}

ZTEST(test_is31_offsets, test_offset_row0_col11)
{
	int off = matrix_led_offset(0, 11);

	/* (0*39 + 11) = 11 — still page 0 */
	zassert_equal(off, 11, "LED(0,11) offset should be 11, got %d", off);
	zassert_true(off < 180, "LED(0,11) should be on page 0");
}

ZTEST(test_is31_offsets, test_offset_page_boundary_page0)
{
	/* Highest offset fully inside page 0: row 4, col 11 → 4*39+11=167 */
	int off = matrix_led_offset(4, 11);

	zassert_equal(off, 167, "LED(4,11) offset should be 167, got %d", off);
	zassert_true(off < 180, "LED(4,11) should be in page 0");
}

ZTEST(test_is31_offsets, test_offset_page1_first)
{
	/* First LED in page 1: row 5, col 0 → 5*39=195; reg = 195-180=15 */
	int off = matrix_led_offset(5, 0);

	zassert_equal(off, 195, "LED(5,0) offset should be 195, got %d", off);
	zassert_true(off >= 180, "LED(5,0) should be on page 1");
	zassert_equal(off - 180, 15,
		"LED(5,0) page-1 register should be 15, got %d", off - 180);
}

ZTEST(test_is31_offsets, test_offset_last_used)
{
	/* Last LED we use: row 7, col 11 → 7*39+11=284; reg = 284-180=104 */
	int off = matrix_led_offset(7, 11);

	zassert_equal(off, 284, "LED(7,11) offset should be 284, got %d", off);
	zassert_equal(off - 180, 104,
		"LED(7,11) page-1 register should be 104, got %d", off - 180);
}

ZTEST(test_is31_offsets, test_all_leds_fit_in_pages_01)
{
	/* All 8×12 LED offsets must fit within pages 0 and 1 (0..350). */
	for (int row = 0; row < MATRIX_ROWS; row++) {
		for (int col = 0; col < MATRIX_COLS; col++) {
			int off = matrix_led_offset(row, col);

			zassert_true(off >= 0 && off <= 350,
				"LED(%d,%d) offset %d out of range",
				row, col, off);
		}
	}
}
