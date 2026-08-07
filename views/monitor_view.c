#include "monitor_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * One set of constants for the C and for tools_gen_mockups.py, which mirrors
 * this file line for line so a collision shows up in the README before it ever
 * ships on a device.
 */
#define MON_GAUGE_CX 29
#define MON_GAUGE_CY 46
#define MON_R_OUT 24
#define MON_R_IN 19

#define MON_SCORE_BASE 46 /* baseline of the big number, at the pivot     */
#define MON_SCORE_CAP 36 /* caption above it, where the channel is widest */

#define MON_COL_X 59 /* left edge of the right-hand column          */
#define MON_V1_BASE 24 /* verdict, first line                         */
#define MON_V2_BASE 35 /* verdict, second line                        */
#define MON_V_ONE_BASE 30 /* verdict when it is a single word            */
#define MON_CTX_BASE 46 /* the context line under the verdict          */

#define MON_STRIP_Y 52 /* the eleven check cells                      */
#define MON_DOTS_Y 62 /* page indicator                              */

struct MonitorView {
    View* view;
    MonitorViewCallback cb;
    void* ctx;
};

typedef struct {
    MrdDetect det;
    MrdFix fix;
    bool link_up;
    bool demo;
    uint8_t scenario;
    uint32_t sentences;
    uint32_t bad_checksum;
    uint32_t epochs;
    uint32_t elapsed_s;
    bool hint;
    uint32_t frame;
} MonitorModel;

static void emit(MonitorView* v, MrdViewEvent event) {
    if(v->cb) v->cb(v->ctx, (uint32_t)event);
}

/* ------------------------------------------------------------- pieces -- */

/** Inverted pill, for the two things that must not be mistaken for anything
 * else: that this is simulated data, and that something is jamming. */
static void draw_badge(Canvas* canvas, int x, int y, const char* text) {
    canvas_set_font(canvas, FontSecondary);
    int w = canvas_string_width(canvas, text) + 6;
    canvas_draw_rbox(canvas, x, y, w, 11, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, x + 3, y + 8, text);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_header(Canvas* canvas, const MonitorModel* m) {
    char right[16];

    if(m->demo) {
        right[0] = '\0';
    } else if(!m->link_up) {
        snprintf(right, sizeof(right), "NO LINK");
    } else {
        snprintf(right, sizeof(right), "%u SAT", (unsigned)m->fix.sats_used);
    }

    mrd_ui_header(canvas, "MONITOR", right);

    if(m->demo) {
        canvas_set_font(canvas, FontSecondary);
        int w = canvas_string_width(canvas, "DEMO") + 6;
        canvas_draw_box(canvas, MRD_W - w, 0, w, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, MRD_W - w + 3, MRD_HDR_BASE, "DEMO");
        canvas_set_color(canvas, ColorBlack);
    }
}

/**
 * The state that costs people the most time: everything is fine except that no
 * bytes are arriving. Saying exactly which pins and which baud rate the app is
 * currently expecting turns a mystery into a thirty-second fix.
 */
static void draw_no_link(Canvas* canvas, const MonitorModel* m) {
    canvas_set_font(canvas, FontPrimary);

    if(m->sentences == 0) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignBottom, "Waiting for");
        canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignBottom, "the receiver");

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 47, AlignCenter, AlignBottom, "No NMEA on the port yet");
    } else {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignBottom, "Receiver");
        canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignBottom, "went quiet");

        canvas_set_font(canvas, FontSecondary);
        char line[32];
        snprintf(line, sizeof(line), "%lu sentences before that", (unsigned long)m->sentences);
        canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignBottom, line);
    }

    canvas_draw_line(canvas, 0, 51, MRD_W - 1, 51);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, "Menu > Wiring for pinout");
}

static void draw_context(Canvas* canvas, const MonitorModel* m) {
    if(m->det.jamming) {
        draw_badge(canvas, MON_COL_X, MON_CTX_BASE - 8, "JAMMING");
        return;
    }

    char line[24];
    canvas_set_font(canvas, FontSecondary);

    /* 69 px to work with. Both of these were measured against the mockup
     * renderer rather than guessed - the earlier collision here was the gauge's
     * end ticks, not the text, and shortening the label would have hidden the
     * real bug rather than fixed it. */
    if(m->det.verdict >= MrdVerdictAnomalous) {
        snprintf(line, sizeof(line), "%u of 4 paths", (unsigned)m->det.families);
    } else {
        snprintf(line, sizeof(line), "%u of %u armed", (unsigned)m->det.armed, MrdCheckCount);
    }
    canvas_draw_str(canvas, MON_COL_X, MON_CTX_BASE, line);
}

static void draw_verdict(Canvas* canvas, const MonitorModel* m) {
    char w1[16], w2[16];
    mrd_ui_split_words(mrd_verdict_name((MrdVerdict)m->det.verdict), w1, sizeof(w1), w2, sizeof(w2));

    canvas_set_font(canvas, FontPrimary);
    if(w2[0]) {
        canvas_draw_str(canvas, MON_COL_X, MON_V1_BASE, w1);
        canvas_draw_str(canvas, MON_COL_X, MON_V2_BASE, w2);
    } else {
        canvas_draw_str(canvas, MON_COL_X, MON_V_ONE_BASE, w1);
    }

    draw_context(canvas, m);
}

static void draw_gauge(Canvas* canvas, const MonitorModel* m) {
    mrd_ui_arc_gauge(canvas, MON_GAUGE_CX, MON_GAUGE_CY, MON_R_OUT, MON_R_IN, m->det.score);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, MON_GAUGE_CX, MON_SCORE_CAP, AlignCenter, AlignBottom, "SCORE");

    char num[8];
    snprintf(num, sizeof(num), "%u", (unsigned)m->det.score);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, MON_GAUGE_CX, MON_SCORE_BASE, AlignCenter, AlignBottom, num);
}

/** Replaces the strip for a few seconds when the screen opens. Nothing about
 * left/right paging is discoverable otherwise. */
static void draw_hint(Canvas* canvas) {
    canvas_draw_box(canvas, 0, MON_STRIP_Y - 1, MRD_W, 13);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, MON_STRIP_Y + 8, "< >  pages");
    canvas_draw_str_aligned(
        canvas, MRD_W - 4, MON_STRIP_Y + 8, AlignRight, AlignBottom, "OK  evidence");
    canvas_set_color(canvas, ColorBlack);
}

/* --------------------------------------------------------------- draw -- */

static void monitor_view_draw(Canvas* canvas, void* model) {
    const MonitorModel* m = model;

    canvas_clear(canvas);
    draw_header(canvas, m);

    /* Live, and nothing has ever arrived, or the module stopped talking. Demo
     * mode always has data by construction. */
    if(!m->demo && !m->link_up) {
        draw_no_link(canvas, m);
        return;
    }

    draw_gauge(canvas, m);
    draw_verdict(canvas, m);

    if(m->hint) {
        draw_hint(canvas);
    } else {
        canvas_draw_line(canvas, 0, MON_STRIP_Y - 2, MRD_W - 1, MON_STRIP_Y - 2);
        mrd_ui_check_strip(canvas, 4, MON_STRIP_Y, &m->det, -1);
        mrd_ui_page_dots(canvas, 64, MON_DOTS_Y, MRD_PAGE_COUNT, 0);
    }
}

/* -------------------------------------------------------------- input -- */

static bool monitor_view_input(InputEvent* event, void* context) {
    MonitorView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyRight:
        emit(v, MrdViewEventPageNext);
        return true;
    case InputKeyLeft:
        emit(v, MrdViewEventPagePrev);
        return true;
    case InputKeyOk:
        emit(v, MrdViewEventDetail);
        return true;
    default:
        return false;
    }
}

/* --------------------------------------------------------------- glue -- */

MonitorView* monitor_view_alloc(void) {
    MonitorView* v = malloc(sizeof(MonitorView));
    memset(v, 0, sizeof(MonitorView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(MonitorModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, monitor_view_draw);
    view_set_input_callback(v->view, monitor_view_input);

    return v;
}

void monitor_view_free(MonitorView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* monitor_view_get_view(MonitorView* v) {
    return v->view;
}

void monitor_view_set_callback(MonitorView* v, MonitorViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void monitor_view_update(
    MonitorView* v,
    const MrdSnapshot* snap,
    uint32_t elapsed_s,
    bool hint) {
    furi_assert(v);
    with_view_model(
        v->view,
        MonitorModel * m,
        {
            m->det = snap->det;
            m->fix = snap->fix;
            m->link_up = snap->link_up;
            m->demo = snap->demo;
            m->scenario = snap->scenario;
            m->sentences = snap->sentences;
            m->bad_checksum = snap->bad_checksum;
            m->epochs = snap->epochs;
            m->elapsed_s = elapsed_s;
            m->hint = hint;
            m->frame++;
        },
        true);
}
