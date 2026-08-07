#include "evidence_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * Mirrored in tools_gen_mockups.py.
 */
#define EV_TOP 12
#define EV_ROW_H 12
#define EV_ROWS 3
#define EV_TEXT_DY 9 /* baseline within a row      */

#define EV_COL_GLYPH 2
#define EV_COL_NAME 13
#define EV_COL_HITS 116 /* right edge, clear of the scrollbar */

#define EV_RULE_Y 49
#define EV_OBS_BASE 58

struct EvidenceView {
    View* view;
    EvidenceViewCallback cb;
    void* ctx;
};

typedef struct {
    MrdDetect det;
    uint8_t sel;
    uint8_t top; /**< first visible row */
} EvidenceModel;

static void emit(EvidenceView* v, MrdViewEvent event) {
    if(v->cb) v->cb(v->ctx, (uint32_t)event);
}

/* -------------------------------------------------------------- glyph -- */

/** Same vocabulary as the monitor's check strip, at row scale: solid is an
 * alert, half-filled a warning, an outline means it ran and found nothing, and
 * corner dots mean it has not had the data to run at all. */
static void draw_glyph(Canvas* canvas, int x, int y, uint8_t state) {
    const int s = 7;
    switch(state) {
    case MrdStateAlert:
        canvas_draw_box(canvas, x, y, s, s);
        break;
    case MrdStateWarn:
        canvas_draw_frame(canvas, x, y, s, s);
        canvas_draw_box(canvas, x + 2, y + 4, s - 4, 2);
        break;
    case MrdStateOk:
        canvas_draw_frame(canvas, x, y, s, s);
        break;
    default:
        canvas_draw_dot(canvas, x, y);
        canvas_draw_dot(canvas, x + s - 1, y);
        canvas_draw_dot(canvas, x, y + s - 1);
        canvas_draw_dot(canvas, x + s - 1, y + s - 1);
        break;
    }
}

/* --------------------------------------------------------------- draw -- */

static void evidence_view_draw(Canvas* canvas, void* model) {
    const EvidenceModel* m = model;

    canvas_clear(canvas);

    char right[16];
    if(m->det.alerts > 0) {
        snprintf(right, sizeof(right), "%u alerting", (unsigned)m->det.alerts);
    } else if(m->det.warns > 0) {
        snprintf(right, sizeof(right), "%u warning", (unsigned)m->det.warns);
    } else {
        snprintf(right, sizeof(right), "%u armed", (unsigned)m->det.armed);
    }
    mrd_ui_header(canvas, "EVIDENCE", right);

    for(uint8_t r = 0; r < EV_ROWS; r++) {
        uint8_t i = (uint8_t)(m->top + r);
        if(i >= MrdCheckCount) break;

        int y = EV_TOP + r * EV_ROW_H;
        bool selected = (i == m->sel);

        if(selected) {
            canvas_draw_box(canvas, 0, y, MRD_W - 4, EV_ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }

        draw_glyph(canvas, EV_COL_GLYPH, y + 3, m->det.checks[i].state);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, EV_COL_NAME, y + EV_TEXT_DY, mrd_check_name((MrdCheckId)i));

        if(m->det.checks[i].hits > 0) {
            char hits[10];
            snprintf(hits, sizeof(hits), "x%u", (unsigned)m->det.checks[i].hits);
            canvas_draw_str_aligned(
                canvas, EV_COL_HITS, y + EV_TEXT_DY, AlignRight, AlignBottom, hits);
        }

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    elements_scrollbar(canvas, m->sel, MrdCheckCount);

    /* The observation behind the cursor's check. This is the line that turns
     * "something is wrong" into "here is the number and here is what it should
     * have been" - the difference between an alarm and an argument. */
    canvas_draw_line(canvas, 0, EV_RULE_Y, MRD_W - 1, EV_RULE_Y);
    canvas_set_font(canvas, FontSecondary);

    char obs[64];
    mrd_check_observed(&m->det, (MrdCheckId)m->sel, obs, sizeof(obs));
    canvas_draw_str(canvas, 2, EV_OBS_BASE, obs);

    mrd_ui_page_dots(canvas, 64, 62, MRD_PAGE_COUNT, 3);
}

/* -------------------------------------------------------------- input -- */

static void scroll_to(EvidenceModel* m, int delta) {
    int sel = (int)m->sel + delta;
    if(sel < 0) sel = MrdCheckCount - 1;
    if(sel >= MrdCheckCount) sel = 0;
    m->sel = (uint8_t)sel;

    /* Keep the cursor on screen with the minimum movement, so the list does
     * not jump under the user's eye. */
    if(m->sel < m->top) m->top = m->sel;
    if(m->sel >= m->top + EV_ROWS) m->top = (uint8_t)(m->sel - EV_ROWS + 1);
    if(m->top > MrdCheckCount - EV_ROWS) m->top = MrdCheckCount - EV_ROWS;
}

static bool evidence_view_input(InputEvent* event, void* context) {
    EvidenceView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyUp:
        with_view_model(v->view, EvidenceModel * m, { scroll_to(m, -1); }, true);
        return true;
    case InputKeyDown:
        with_view_model(v->view, EvidenceModel * m, { scroll_to(m, 1); }, true);
        return true;
    case InputKeyOk:
        emit(v, MrdViewEventDetail);
        return true;
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

EvidenceView* evidence_view_alloc(void) {
    EvidenceView* v = malloc(sizeof(EvidenceView));
    memset(v, 0, sizeof(EvidenceView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(EvidenceModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, evidence_view_draw);
    view_set_input_callback(v->view, evidence_view_input);

    return v;
}

void evidence_view_free(EvidenceView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* evidence_view_get_view(EvidenceView* v) {
    return v->view;
}

void evidence_view_set_callback(EvidenceView* v, EvidenceViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void evidence_view_update(EvidenceView* v, const MrdSnapshot* snap) {
    furi_assert(v);
    with_view_model(v->view, EvidenceModel * m, { m->det = snap->det; }, true);
}

uint8_t evidence_view_selected(EvidenceView* v) {
    uint8_t sel = 0;
    with_view_model(v->view, EvidenceModel * m, { sel = m->sel; }, false);
    return sel;
}
