#include "mrd_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define UI_PI 3.14159265f

/* ------------------------------------------------------------- gauge -- */

void mrd_ui_arc_gauge(Canvas* canvas, int cx, int cy, int r_out, int r_in, uint8_t pct) {
    /* Enough spokes that the fill is solid at the outer radius: the arc is
     * about pi*r pixels long, and 64 steps keeps them under 1.5 px apart at
     * the radii this app uses. */
    const int STEPS = 64;
    if(pct > 100) pct = 100;
    int on = ((int)pct * STEPS + 50) / 100;

    /* Both rims, drawn continuously across the whole sweep. An empty gauge has
     * to read as an empty channel rather than as dust on the screen, so the
     * track is a shape in its own right and the score fills it. */
    int px_o = 0, py_o = 0, px_i = 0, py_i = 0;
    for(int i = 0; i <= STEPS; i++) {
        float a = UI_PI * (1.0f - (float)i / (float)STEPS);
        float ca = cosf(a), sa = sinf(a);

        int xo = cx + (int)(ca * (float)r_out);
        int yo = cy - (int)(sa * (float)r_out);
        int xi = cx + (int)(ca * (float)r_in);
        int yi = cy - (int)(sa * (float)r_in);

        if(i > 0) {
            canvas_draw_line(canvas, px_o, py_o, xo, yo);
            canvas_draw_line(canvas, px_i, py_i, xi, yi);
        }
        px_o = xo;
        py_o = yo;
        px_i = xi;
        py_i = yi;
    }

    for(int i = 0; i < on; i++) {
        float a = UI_PI * (1.0f - ((float)i + 0.5f) / (float)STEPS);
        float ca = cosf(a), sa = sinf(a);
        canvas_draw_line(
            canvas,
            cx + (int)(ca * (float)r_in),
            cy - (int)(sa * (float)r_in),
            cx + (int)(ca * (float)r_out),
            cy - (int)(sa * (float)r_out));
    }

    /* Quarter ticks, inside the channel. The ends are left alone: a tick at
     * 0 or 100 would stick out horizontally into the text beside the gauge. */
    for(int q = 1; q <= 3; q++) {
        float a = UI_PI * (1.0f - (float)q / 4.0f);
        float ca = cosf(a), sa = sinf(a);
        int x0 = cx + (int)(ca * (float)(r_out + 1));
        int y0 = cy - (int)(sa * (float)(r_out + 1));
        int x1 = cx + (int)(ca * (float)(r_out + 3));
        int y1 = cy - (int)(sa * (float)(r_out + 3));
        canvas_draw_line(canvas, x0, y0, x1, y1);
    }
}

/* -------------------------------------------------------- check strip -- */

#define STRIP_CELL_W 10
#define STRIP_CELL_H 9
#define STRIP_GAP 1

void mrd_ui_check_strip(Canvas* canvas, int x, int y, const MrdDetect* det, int selected) {
    for(int i = 0; i < MrdCheckCount; i++) {
        int cx = x + i * (STRIP_CELL_W + STRIP_GAP);
        const MrdCheck* c = &det->checks[i];

        switch(c->state) {
        case MrdStateAlert:
            canvas_draw_box(canvas, cx, y, STRIP_CELL_W, STRIP_CELL_H);
            break;
        case MrdStateWarn:
            canvas_draw_frame(canvas, cx, y, STRIP_CELL_W, STRIP_CELL_H);
            canvas_draw_box(
                canvas, cx + 2, y + STRIP_CELL_H - 4, STRIP_CELL_W - 4, 2);
            break;
        case MrdStateOk:
            canvas_draw_frame(canvas, cx, y, STRIP_CELL_W, STRIP_CELL_H);
            break;
        default:
            /* Not armed. Present, but visibly not yet reporting - the count of
             * checks that have not run is itself information. */
            canvas_draw_dot(canvas, cx, y);
            canvas_draw_dot(canvas, cx + STRIP_CELL_W - 1, y);
            canvas_draw_dot(canvas, cx, y + STRIP_CELL_H - 1);
            canvas_draw_dot(canvas, cx + STRIP_CELL_W - 1, y + STRIP_CELL_H - 1);
            break;
        }

        if(selected == i) {
            canvas_draw_line(
                canvas, cx, y + STRIP_CELL_H + 1, cx + STRIP_CELL_W - 1, y + STRIP_CELL_H + 1);
        }
    }
}

/* ------------------------------------------------------------ header -- */

void mrd_ui_header(Canvas* canvas, const char* title, const char* right) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, MRD_HDR_BASE, title);
    if(right && right[0]) {
        canvas_draw_str_aligned(
            canvas, MRD_W - 2, MRD_HDR_BASE, AlignRight, AlignBottom, right);
    }
    canvas_draw_line(canvas, 0, MRD_HDR_RULE, MRD_W - 1, MRD_HDR_RULE);
}

void mrd_ui_page_dots(Canvas* canvas, int cx, int y, uint8_t count, uint8_t active) {
    int span = (count - 1) * 5;
    int x = cx - span / 2;
    for(uint8_t i = 0; i < count; i++) {
        if(i == active) {
            canvas_draw_box(canvas, x + i * 5 - 1, y - 1, 3, 3);
        } else {
            canvas_draw_dot(canvas, x + i * 5, y);
        }
    }
}

/* ------------------------------------------------------------- globe -- */

void mrd_ui_globe(Canvas* canvas, int cx, int cy, int r, uint32_t phase) {
    canvas_draw_circle(canvas, cx, cy, r);

    /* Two latitudes, as chords. */
    for(int k = -1; k <= 1; k += 2) {
        int dy = (r * k) / 2;
        int half = (int)(sqrtf((float)(r * r - dy * dy)));
        canvas_draw_line(canvas, cx - half, cy + dy, cx + half, cy + dy);
    }

    /* The meridian itself, drawn as an ellipse whose width breathes with the
     * phase - the globe turning under a line that stays put. */
    float t = (float)(phase % 64u) / 64.0f;
    float w = cosf(t * 2.0f * UI_PI);
    int half_w = (int)(w * (float)r);

    int prev_x = cx, prev_y = cy - r;
    for(int i = 1; i <= 16; i++) {
        float a = UI_PI * ((float)i / 16.0f);
        int px = cx + (int)((float)half_w * sinf(a));
        int py = cy - (int)((float)r * cosf(a));
        canvas_draw_line(canvas, prev_x, prev_y, px, py);
        prev_x = px;
        prev_y = py;
    }
    prev_x = cx;
    prev_y = cy - r;
    for(int i = 1; i <= 16; i++) {
        float a = UI_PI * ((float)i / 16.0f);
        int px = cx - (int)((float)half_w * sinf(a));
        int py = cy - (int)((float)r * cosf(a));
        canvas_draw_line(canvas, prev_x, prev_y, px, py);
        prev_x = px;
        prev_y = py;
    }
}

/* -------------------------------------------------------------- text -- */

void mrd_ui_fmt1(char* out, size_t len, float v) {
    bool neg = v < 0.0f;
    if(neg) v = -v;
    if(v > 99999.0f) v = 99999.0f;
    uint32_t w = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)w) * 10.0f + 0.5f);
    if(f >= 10) {
        f = 0;
        w++;
    }
    snprintf(out, len, "%s%lu.%lu", neg ? "-" : "", (unsigned long)w, (unsigned long)f);
}

void mrd_ui_fmt2(char* out, size_t len, float v) {
    bool neg = v < 0.0f;
    if(neg) v = -v;
    if(v > 9999.0f) v = 9999.0f;
    uint32_t w = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)w) * 100.0f + 0.5f);
    if(f >= 100) {
        f = 0;
        w++;
    }
    snprintf(out, len, "%s%lu.%02lu", neg ? "-" : "", (unsigned long)w, (unsigned long)f);
}

/** Five decimal places is about a metre, which is the resolution the fix is
 * actually good to. Printing more would be inventing precision. */
static void fmt_deg(char* out, size_t len, double deg, char pos, char neg) {
    char hemi = (deg < MRD_D(0)) ? neg : pos;
    if(deg < MRD_D(0)) deg = -deg;
    if(deg > MRD_D(180)) deg = MRD_D(180);

    uint32_t w = (uint32_t)deg;
    uint32_t f = (uint32_t)((deg - (double)w) * MRD_D(100000) + MRD_D(0.5));
    if(f >= 100000) {
        f = 0;
        w++;
    }
    snprintf(out, len, "%u.%05u%c", (unsigned)w, (unsigned)f, hemi);
}

void mrd_ui_fmt_lat(char* out, size_t len, double deg) {
    fmt_deg(out, len, deg, 'N', 'S');
}

void mrd_ui_fmt_lon(char* out, size_t len, double deg) {
    fmt_deg(out, len, deg, 'E', 'W');
}

void mrd_ui_elapsed(char* out, size_t len, uint32_t seconds) {
    if(seconds < 60) {
        snprintf(out, len, "%lus", (unsigned long)seconds);
    } else if(seconds < 3600) {
        snprintf(
            out, len, "%lum %02lus", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    } else {
        snprintf(
            out,
            len,
            "%luh %02lum",
            (unsigned long)(seconds / 3600),
            (unsigned long)((seconds / 60) % 60));
    }
}

void mrd_ui_split_words(const char* text, char* a, size_t alen, char* b, size_t blen) {
    a[0] = '\0';
    b[0] = '\0';

    const char* space = strchr(text, ' ');
    if(!space) {
        strncpy(a, text, alen - 1);
        a[alen - 1] = '\0';
        return;
    }

    size_t first = (size_t)(space - text);
    if(first >= alen) first = alen - 1;
    memcpy(a, text, first);
    a[first] = '\0';

    strncpy(b, space + 1, blen - 1);
    b[blen - 1] = '\0';
}
