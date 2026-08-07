#include "sky_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * Mirrored in tools_gen_mockups.py.
 */
#define SKY_CX 33 /* polar plot centre                          */
#define SKY_CY 37
#define SKY_R 23 /* the horizon, i.e. zero degrees elevation   */

#define SKY_INFO_X 61 /* satellite detail column                    */

#define BAR_TOP 14 /* C/N0 chart: top of the plotting area       */
#define BAR_BASE 50 /* the zero line                              */
#define BAR_MAX_DB 55 /* full scale, dB-Hz                          */
#define BAR_W 3 /* narrowest, when the sky is full           */
#define BAR_MAX_W 9 /* widest, so a sparse sky still reads as bars */
#define BAR_GAP 1

#define SKY_FOOT_BASE 61 /* statistics strip baseline                  */

struct SkyView {
    View* view;
    SkyViewCallback cb;
    void* ctx;
};

typedef struct {
    MrdSat sats[MRD_MAX_SATS];
    uint8_t sat_count;
    MrdFix fix;
    float snr_mean;
    float snr_sigma;
    float snr_elev_r;
    uint8_t tracked;
    bool demo;

    bool bars; /**< false: polar sky. true: C/N0 bar chart. */
    uint8_t sel; /**< selected satellite, polar mode only */
} SkyModel;

static void emit(SkyView* v, MrdViewEvent event) {
    if(v->cb) v->cb(v->ctx, (uint32_t)event);
}

/* ------------------------------------------------------------- polar -- */

/**
 * Standard sky plot: north up, east right, the horizon at the rim and the
 * zenith at the centre. Elevation maps linearly to radius, which is what every
 * receiver's own plot does, so the picture is comparable with one.
 */
static void sat_xy(const MrdSat* s, int* x, int* y) {
    float r = (float)SKY_R * (90.0f - (float)s->elev) / 90.0f;
    float a = ((float)s->azim - 90.0f) * 0.01745329f; /* 0 deg = north = up */
    *x = SKY_CX + (int)(r * cosf(a));
    *y = SKY_CY + (int)(r * sinf(a));
}

static void draw_polar(Canvas* canvas, const SkyModel* m) {
    canvas_draw_circle(canvas, SKY_CX, SKY_CY, SKY_R);
    canvas_draw_circle(canvas, SKY_CX, SKY_CY, (SKY_R * 2) / 3); /* 30 deg */
    canvas_draw_circle(canvas, SKY_CX, SKY_CY, SKY_R / 3); /* 60 deg */

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, SKY_CX - 2, SKY_CY - SKY_R - 1, "N");

    /* Cardinal ticks, so the rim reads as a compass rather than a circle. */
    canvas_draw_line(canvas, SKY_CX, SKY_CY - SKY_R, SKY_CX, SKY_CY - SKY_R + 3);
    canvas_draw_line(canvas, SKY_CX, SKY_CY + SKY_R, SKY_CX, SKY_CY + SKY_R - 3);
    canvas_draw_line(canvas, SKY_CX - SKY_R, SKY_CY, SKY_CX - SKY_R + 3, SKY_CY);
    canvas_draw_line(canvas, SKY_CX + SKY_R, SKY_CY, SKY_CX + SKY_R - 3, SKY_CY);

    for(uint8_t i = 0; i < m->sat_count; i++) {
        const MrdSat* s = &m->sats[i];
        int x, y;
        sat_xy(s, &x, &y);

        if(s->snr == 0) {
            /* In view, not tracked. Drawn hollow and small: it is real
             * information that the receiver can see it but not use it. */
            canvas_draw_dot(canvas, x, y);
        } else {
            /* Size carries C/N0, so a sky of identically sized dots is itself
             * the flat-power tell, visible before you read a single number. */
            int sz = (s->snr >= 42) ? 3 : (s->snr >= 33) ? 2 : 1;
            if(s->used) {
                canvas_draw_box(canvas, x - sz, y - sz, sz * 2 + 1, sz * 2 + 1);
            } else {
                canvas_draw_frame(canvas, x - sz, y - sz, sz * 2 + 1, sz * 2 + 1);
            }
        }

        if(i == m->sel) canvas_draw_circle(canvas, x, y, 5);
    }
}

static void draw_polar_info(Canvas* canvas, const SkyModel* m) {
    canvas_set_font(canvas, FontSecondary);

    if(m->sat_count == 0) {
        canvas_draw_str(canvas, SKY_INFO_X, 26, "No satellites");
        canvas_draw_str(canvas, SKY_INFO_X, 36, "reported yet.");
        return;
    }

    const MrdSat* s = &m->sats[m->sel < m->sat_count ? m->sel : 0];
    char line[24];

    snprintf(line, sizeof(line), "%s %u", mrd_system_short(s->sys), (unsigned)s->prn);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, SKY_INFO_X, 21, line);

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "el %u  az %u", (unsigned)s->elev, (unsigned)s->azim);
    canvas_draw_str(canvas, SKY_INFO_X, 31, line);

    if(s->snr > 0) {
        snprintf(line, sizeof(line), "%u dB-Hz", (unsigned)s->snr);
    } else {
        snprintf(line, sizeof(line), "not tracked");
    }
    canvas_draw_str(canvas, SKY_INFO_X, 41, line);

    canvas_draw_str(canvas, SKY_INFO_X, 51, s->used ? "in the fix" : "not in fix");
}

/* -------------------------------------------------------------- bars -- */

/**
 * The screen this whole application is really about.
 *
 * An honest constellation makes a ragged skyline: satellites near the horizon
 * come in weak, satellites overhead come in strong, and the spread is four to
 * ten decibels. A transmitter feeding every channel from one amplifier makes a
 * flat wall at one height. You do not have to read the sigma underneath to see
 * which one you are looking at.
 */
static void draw_bars(Canvas* canvas, const SkyModel* m) {
    uint8_t n = m->sat_count;
    if(n == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 34, AlignCenter, AlignBottom, "No satellites reported yet");
        return;
    }

    /*
     * Bars widen to fill the screen when few satellites are up. A ten-bar chart
     * squeezed into the middle third wastes the resolution this screen exists
     * to show - the whole point is being able to see three decibels of
     * difference between neighbours.
     */
    int bar_w = (MRD_W - 4) / (int)n - BAR_GAP;
    if(bar_w < BAR_W) bar_w = BAR_W;
    if(bar_w > BAR_MAX_W) bar_w = BAR_MAX_W;

    int step = bar_w + BAR_GAP;
    int total = (int)n * step - BAR_GAP;
    int x0 = (MRD_W - total) / 2;
    if(x0 < 1) x0 = 1;

    canvas_draw_line(canvas, 0, BAR_BASE, MRD_W - 1, BAR_BASE);

    for(uint8_t i = 0; i < n; i++) {
        int x = x0 + (int)i * step;
        if(x + bar_w > MRD_W - 1) break;

        uint8_t snr = m->sats[i].snr;
        if(snr == 0) {
            /* Untracked: a stub on the baseline, so it is still counted but
             * cannot be mistaken for a weak lock. */
            canvas_draw_line(canvas, x, BAR_BASE - 1, x + bar_w - 1, BAR_BASE - 1);
            continue;
        }

        int h = (int)snr * (BAR_BASE - BAR_TOP) / BAR_MAX_DB;
        if(h > BAR_BASE - BAR_TOP) h = BAR_BASE - BAR_TOP;
        if(h < 1) h = 1;

        if(m->sats[i].used) {
            canvas_draw_box(canvas, x, BAR_BASE - h, bar_w, h);
        } else {
            canvas_draw_frame(canvas, x, BAR_BASE - h, bar_w, h);
        }
    }

    /* The mean, as a dashed rule across the chart. With the bars, it makes the
     * spread a shape rather than a statistic. */
    if(m->tracked > 0) {
        int mh = (int)m->snr_mean * (BAR_BASE - BAR_TOP) / BAR_MAX_DB;
        int my = BAR_BASE - mh;
        if(my > BAR_TOP && my < BAR_BASE) {
            for(int x = 0; x < MRD_W; x += 4) canvas_draw_dot(canvas, x, my);
        }
    }
}

/* --------------------------------------------------------------- feet -- */

/** The three carrier statistics, always on screen in both modes. These are the
 * numbers behind three of the eleven checks; showing them is how the app makes
 * its reasoning inspectable instead of asking to be trusted. */
static void draw_stats(Canvas* canvas, const SkyModel* m) {
    canvas_draw_line(canvas, 0, SKY_FOOT_BASE - 9, MRD_W - 1, SKY_FOOT_BASE - 9);
    canvas_set_font(canvas, FontSecondary);

    if(m->tracked == 0) {
        canvas_draw_str(canvas, 2, SKY_FOOT_BASE, "nothing tracked");
        return;
    }

    char buf[12], line[40];
    mrd_ui_fmt1(buf, sizeof(buf), m->snr_mean);
    snprintf(line, sizeof(line), "avg %s", buf);
    canvas_draw_str(canvas, 2, SKY_FOOT_BASE, line);

    mrd_ui_fmt1(buf, sizeof(buf), m->snr_sigma);
    snprintf(line, sizeof(line), "sd %s", buf);
    canvas_draw_str(canvas, 46, SKY_FOOT_BASE, line);

    mrd_ui_fmt2(buf, sizeof(buf), m->snr_elev_r);
    snprintf(line, sizeof(line), "r %s", buf);
    canvas_draw_str_aligned(canvas, MRD_W - 2, SKY_FOOT_BASE, AlignRight, AlignBottom, line);
}

/* --------------------------------------------------------------- draw -- */

static void sky_view_draw(Canvas* canvas, void* model) {
    const SkyModel* m = model;

    canvas_clear(canvas);

    char right[16];
    snprintf(right, sizeof(right), "%u/%u trk", (unsigned)m->tracked, (unsigned)m->sat_count);
    mrd_ui_header(canvas, m->bars ? "CARRIERS" : "SKY", right);

    if(m->bars) {
        draw_bars(canvas, m);
    } else {
        draw_polar(canvas, m);
        draw_polar_info(canvas, m);
    }

    draw_stats(canvas, m);
    mrd_ui_page_dots(canvas, 64, 62, MRD_PAGE_COUNT, 1);
}

/* -------------------------------------------------------------- input -- */

static bool sky_view_input(InputEvent* event, void* context) {
    SkyView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = false;

    switch(event->key) {
    case InputKeyOk:
        with_view_model(v->view, SkyModel * m, { m->bars = !m->bars; }, true);
        consumed = true;
        break;

    case InputKeyUp:
    case InputKeyDown: {
        /* Selection only means something on the polar plot; in bar mode both
         * keys are left alone so they can do nothing visible rather than move
         * an invisible cursor. */
        bool moved = false;
        with_view_model(
            v->view,
            SkyModel * m,
            {
                if(!m->bars && m->sat_count > 0) {
                    if(event->key == InputKeyDown) {
                        m->sel = (uint8_t)((m->sel + 1) % m->sat_count);
                    } else {
                        m->sel = (uint8_t)((m->sel + m->sat_count - 1) % m->sat_count);
                    }
                    moved = true;
                }
            },
            true);
        consumed = moved;
        break;
    }

    case InputKeyRight:
        emit(v, MrdViewEventPageNext);
        consumed = true;
        break;

    case InputKeyLeft:
        emit(v, MrdViewEventPagePrev);
        consumed = true;
        break;

    default:
        break;
    }

    return consumed;
}

/* --------------------------------------------------------------- glue -- */

SkyView* sky_view_alloc(void) {
    SkyView* v = malloc(sizeof(SkyView));
    memset(v, 0, sizeof(SkyView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SkyModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, sky_view_draw);
    view_set_input_callback(v->view, sky_view_input);

    return v;
}

void sky_view_free(SkyView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* sky_view_get_view(SkyView* v) {
    return v->view;
}

void sky_view_set_callback(SkyView* v, SkyViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void sky_view_update(SkyView* v, const MrdSnapshot* snap) {
    furi_assert(v);
    with_view_model(
        v->view,
        SkyModel * m,
        {
            memcpy(m->sats, snap->sats, sizeof(m->sats));
            m->sat_count = snap->sat_count;
            m->fix = snap->fix;
            m->snr_mean = snap->det.snr_mean;
            m->snr_sigma = snap->det.snr_sigma;
            m->snr_elev_r = snap->det.snr_elev_r;
            m->tracked = snap->det.sats_tracked;
            m->demo = snap->demo;
            if(m->sel >= m->sat_count) m->sel = 0;
        },
        true);
}
