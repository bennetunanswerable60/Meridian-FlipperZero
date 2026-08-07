#pragma once

/*
 * Drawing primitives shared by every screen, so the app reads as one object
 * rather than six. Anything here that has a geometry is mirrored exactly in
 * tools_gen_mockups.py, which is how the README screenshots stay honest.
 */

#include <gui/gui.h>
#include <stdint.h>

#include "mrd_detect.h"

#define MRD_W 128
#define MRD_H 64

/** Header baseline, the hairline under it, and the first content row. */
#define MRD_HDR_BASE 8
#define MRD_HDR_RULE 10
#define MRD_TOP 12

/**
 * The signature element: a half-dial filled from the left, with a dotted track
 * where it is empty. No needle - a one-pixel needle on a 128x64 screen aliases
 * into mush, and a filled band reads correctly from across a room.
 */
void mrd_ui_arc_gauge(Canvas* canvas, int cx, int cy, int r_out, int r_in, uint8_t pct);

/**
 * Eleven cells, one per check, in fixed order. Empty outline means the check
 * has not had the data to run, a frame means it ran and found nothing, a
 * half-filled cell is a warning and a solid one is an alert. It reads as a
 * barcode: you learn the healthy shape in about two sessions.
 */
void mrd_ui_check_strip(Canvas* canvas, int x, int y, const MrdDetect* det, int selected);

/** Title on the left, status on the right, hairline underneath. */
void mrd_ui_header(Canvas* canvas, const char* title, const char* right);

/** Page dots, so it is obvious there is more than one screen here. */
void mrd_ui_page_dots(Canvas* canvas, int cx, int y, uint8_t count, uint8_t active);

/** The app mark: a globe cut by its meridian. @p phase animates the sweep. */
void mrd_ui_globe(Canvas* canvas, int cx, int cy, int r, uint32_t phase);

/* ---- text, all integer-formatted: newlib-nano leaves %f out of printf ---- */

/** "51.47790N" - five decimals, which is about a metre. */
void mrd_ui_fmt_lat(char* out, size_t len, double deg);
void mrd_ui_fmt_lon(char* out, size_t len, double deg);

/** One and two decimal places. */
void mrd_ui_fmt1(char* out, size_t len, float v);
void mrd_ui_fmt2(char* out, size_t len, float v);

/** "3m 04s" from a count of seconds. */
void mrd_ui_elapsed(char* out, size_t len, uint32_t seconds);

/** Split a verdict into at most two lines that fit @p max_w pixels. */
void mrd_ui_split_words(const char* text, char* a, size_t alen, char* b, size_t blen);
