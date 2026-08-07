#include "trail_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * Mirrored in tools_gen_mockups.py.
 */
#define TRL_CX 64
#define TRL_CY 31
#define TRL_R 17 /* the scale ring, and the radius the plot fits to */

#define TRL_FOOT_RULE 52
#define TRL_FOOT_BASE 61

/** Never zoom in past this, or a perfectly frozen fix scales to infinity and
 * a single recited coordinate would draw as an impressive-looking cloud. */
#define TRL_MIN_SPAN_M 1.0f

struct TrailView {
    View* view;
    TrailViewCallback cb;
    void* ctx;
};

typedef struct {
    MrdTrail trail;
    MrdFix fix;
    uint8_t frozen_run; /**< identical fixes in the last window */
    uint8_t frozen_state;
    bool has_fix;
} TrailModel;

static void emit(TrailView* v, MrdViewEvent event) {
    if(v->cb) v->cb(v->ctx, (uint32_t)event);
}

/* --------------------------------------------------------------- draw -- */

/**
 * What a receiver's own noise looks like.
 *
 * A real fix, standing still, wanders a metre or two a second: thermal noise,
 * multipath, satellites drifting through the solution. The cloud is the proof
 * that a position is being *computed*. A spoofer reciting a constant leaves a
 * single dot, and no amount of confidence in the surrounding numbers looks
 * quite as wrong as that does.
 */
static void trail_view_draw(Canvas* canvas, void* model) {
    const TrailModel* m = model;

    canvas_clear(canvas);

    char right[16];
    snprintf(right, sizeof(right), "%u fixes", (unsigned)m->trail.count);
    mrd_ui_header(canvas, "DRIFT", m->trail.count ? right : "");

    if(m->trail.count < 2) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 30, AlignCenter, AlignBottom, "Waiting for a second fix");
        canvas_draw_str_aligned(
            canvas, 64, 42, AlignCenter, AlignBottom, "The plot needs a few seconds");
        mrd_ui_page_dots(canvas, 64, 62, MRD_PAGE_COUNT, 2);
        return;
    }

    /* Centre on the mean position rather than the latest, so the cloud sits
     * still while you watch it instead of chasing the newest sample. */
    double sum_lat = 0.0, sum_lon = 0.0;
    for(uint8_t i = 0; i < m->trail.count; i++) {
        const MrdTrailPoint* p = mrd_trail_at(&m->trail, i);
        sum_lat += p->lat;
        sum_lon += p->lon;
    }
    double ref_lat = sum_lat / (double)m->trail.count;
    double ref_lon = sum_lon / (double)m->trail.count;

    /* One pass to find the extent, a second to plot: the scale has to be
     * known before the first dot goes down. */
    float max_r = 0.0f;
    for(uint8_t i = 0; i < m->trail.count; i++) {
        const MrdTrailPoint* p = mrd_trail_at(&m->trail, i);
        float e, n;
        mrd_geo_offset_m(ref_lat, ref_lon, p->lat, p->lon, &e, &n);
        float r = sqrtf(e * e + n * n);
        if(r > max_r) max_r = r;
    }
    float span = max_r > TRL_MIN_SPAN_M ? max_r : TRL_MIN_SPAN_M;
    float px_per_m = (float)TRL_R / span;

    /* Scale ring and crosshair. */
    for(int a = 0; a < 360; a += 12) {
        float rad = (float)a * 0.01745329f;
        canvas_draw_dot(
            canvas,
            TRL_CX + (int)(cosf(rad) * (float)TRL_R),
            TRL_CY + (int)(sinf(rad) * (float)TRL_R));
    }
    canvas_draw_line(canvas, TRL_CX - 3, TRL_CY, TRL_CX + 3, TRL_CY);
    canvas_draw_line(canvas, TRL_CX, TRL_CY - 3, TRL_CX, TRL_CY + 3);

    for(uint8_t i = 0; i < m->trail.count; i++) {
        const MrdTrailPoint* p = mrd_trail_at(&m->trail, i);
        float e, n;
        mrd_geo_offset_m(ref_lat, ref_lon, p->lat, p->lon, &e, &n);

        int x = TRL_CX + (int)(e * px_per_m);
        int y = TRL_CY - (int)(n * px_per_m); /* north is up */

        if(i + 1 == m->trail.count) {
            /* The newest fix, boxed, so you can see it move. */
            canvas_draw_box(canvas, x - 1, y - 1, 3, 3);
            canvas_draw_frame(canvas, x - 3, y - 3, 7, 7);
        } else {
            canvas_draw_dot(canvas, x, y);
        }
    }

    /* Ring label, placed inside the ring on the left so it cannot collide with
     * the plot's own right-hand edge. */
    char buf[12];
    canvas_set_font(canvas, FontSecondary);
    mrd_ui_fmt1(buf, sizeof(buf), span);
    char ring[16];
    snprintf(ring, sizeof(ring), "%s m", buf);
    canvas_draw_str(canvas, 2, TRL_CY - TRL_R + 4, ring);

    /* ---- statistics ---- */
    canvas_draw_line(canvas, 0, TRL_FOOT_RULE, MRD_W - 1, TRL_FOOT_RULE);
    canvas_set_font(canvas, FontSecondary);

    mrd_ui_fmt1(buf, sizeof(buf), max_r);
    char line[32];
    snprintf(line, sizeof(line), "spread %s m", buf);
    canvas_draw_str(canvas, 2, TRL_FOOT_BASE, line);

    if(m->frozen_state >= MrdStateWarn) {
        snprintf(line, sizeof(line), "%u identical", (unsigned)m->frozen_run);
    } else if(max_r < 0.2f) {
        snprintf(line, sizeof(line), "not moving");
    } else {
        snprintf(line, sizeof(line), "wandering");
    }
    canvas_draw_str_aligned(canvas, MRD_W - 2, TRL_FOOT_BASE, AlignRight, AlignBottom, line);

    mrd_ui_page_dots(canvas, 64, 62, MRD_PAGE_COUNT, 2);
}

/* -------------------------------------------------------------- input -- */

static bool trail_view_input(InputEvent* event, void* context) {
    TrailView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyRight:
        emit(v, MrdViewEventPageNext);
        return true;
    case InputKeyLeft:
        emit(v, MrdViewEventPagePrev);
        return true;
    default:
        return false;
    }
}

/* --------------------------------------------------------------- glue -- */

TrailView* trail_view_alloc(void) {
    TrailView* v = malloc(sizeof(TrailView));
    memset(v, 0, sizeof(TrailView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(TrailModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, trail_view_draw);
    view_set_input_callback(v->view, trail_view_input);

    return v;
}

void trail_view_free(TrailView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* trail_view_get_view(TrailView* v) {
    return v->view;
}

void trail_view_set_callback(TrailView* v, TrailViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void trail_view_update(TrailView* v, const MrdSnapshot* snap) {
    furi_assert(v);
    with_view_model(
        v->view,
        TrailModel * m,
        {
            m->trail = snap->trail;
            m->fix = snap->fix;
            m->frozen_run = (uint8_t)snap->det.checks[MrdCheckFrozen].value;
            m->frozen_state = snap->det.checks[MrdCheckFrozen].state;
            m->has_fix = snap->fix.valid;
        },
        true);
}
