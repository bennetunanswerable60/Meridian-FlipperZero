#include "wiring_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ---------------- */
#define WIR_BOX_Y 14
#define WIR_BOX_H 34
#define WIR_L_X 2
#define WIR_L_W 38
#define WIR_R_X 88
#define WIR_R_W 38

#define ROW_PWR 20
#define ROW_GND 28
#define ROW_A 36 /* Flipper TX, module RX */
#define ROW_B 44 /* Flipper RX, module TX */

#define WIR_RULE_Y 50
#define WIR_BASE1 58

struct WiringView {
    View* view;
};

typedef struct {
    char port[12];
    char pins[20];
    char baud[10];
    char last[MRD_W / 2];
    bool link_up;
    uint32_t tick;
} WiringModel;

/**
 * The diagram exists for one reason: TX goes to RX. It is the mistake
 * everybody makes once, it produces perfect silence rather than an error, and
 * a drawn crossover explains it faster than a sentence can.
 */
static void wiring_view_draw(Canvas* canvas, void* model) {
    const WiringModel* m = model;

    canvas_clear(canvas);
    mrd_ui_header(canvas, "WIRING", m->link_up ? "LINK UP" : "");

    canvas_set_font(canvas, FontSecondary);

    /* ---- the two boxes ---- */
    canvas_draw_rframe(canvas, WIR_L_X, WIR_BOX_Y, WIR_L_W, WIR_BOX_H, 3);
    canvas_draw_str(canvas, WIR_L_X + 3, WIR_BOX_Y - 2, "FLIPPER");

    canvas_draw_rframe(canvas, WIR_R_X, WIR_BOX_Y, WIR_R_W, WIR_BOX_H, 3);
    canvas_draw_str(canvas, WIR_R_X + 3, WIR_BOX_Y - 2, "GPS");

    /* ---- pin labels ---- */
    canvas_draw_str(canvas, WIR_L_X + 3, ROW_PWR + 3, "3V3");
    canvas_draw_str(canvas, WIR_L_X + 3, ROW_GND + 3, "GND");
    canvas_draw_str(canvas, WIR_L_X + 3, ROW_A + 3, "TX");
    canvas_draw_str(canvas, WIR_L_X + 3, ROW_B + 3, "RX");

    canvas_draw_str(canvas, WIR_R_X + 14, ROW_PWR + 3, "VCC");
    canvas_draw_str(canvas, WIR_R_X + 14, ROW_GND + 3, "GND");
    canvas_draw_str(canvas, WIR_R_X + 17, ROW_A + 3, "TX");
    canvas_draw_str(canvas, WIR_R_X + 17, ROW_B + 3, "RX");

    const int lx = WIR_L_X + WIR_L_W;
    const int rx = WIR_R_X;

    /* ---- power and ground: straight through ---- */
    canvas_draw_line(canvas, lx, ROW_PWR, rx, ROW_PWR);
    canvas_draw_line(canvas, lx, ROW_GND, rx, ROW_GND);

    /* ---- the crossover ---- */
    canvas_draw_line(canvas, lx, ROW_A, rx, ROW_B); /* Flipper TX -> module RX */
    canvas_draw_line(canvas, lx, ROW_B, rx, ROW_A); /* module TX -> Flipper RX */

    /*
     * A pulse crawling right to left along the module's TX line: the direction
     * the sentences actually travel, and the only wire that has to work for
     * this application to do anything at all.
     */
    {
        uint32_t p = (m->tick * 3u) % 40u;
        float f = (float)p / 40.0f;
        int px = rx - (int)((float)(rx - lx) * f);
        int py = ROW_A + (int)((float)(ROW_B - ROW_A) * f);
        canvas_draw_disc(canvas, px, py, 1);
    }

    /* ---- the settings this diagram is describing ---- */
    canvas_draw_line(canvas, 0, WIR_RULE_Y, MRD_W - 1, WIR_RULE_Y);

    char line[56];
    snprintf(line, sizeof(line), "%s  %s  %s", m->port, m->pins, m->baud);
    canvas_draw_str(canvas, 2, WIR_BASE1, line);

    /* The live proof, when there is one. Nothing else on this screen tells you
     * as much as seeing your own receiver's text appear. */
    if(m->last[0]) {
        canvas_draw_str(canvas, 2, WIR_BASE1 + 8, m->last);
    } else {
        canvas_draw_str(canvas, 2, WIR_BASE1 + 8, "Flipper TX goes to module RX");
    }
}

WiringView* wiring_view_alloc(void) {
    WiringView* v = malloc(sizeof(WiringView));
    memset(v, 0, sizeof(WiringView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(WiringModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, wiring_view_draw);

    return v;
}

void wiring_view_free(WiringView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* wiring_view_get_view(WiringView* v) {
    return v->view;
}

void wiring_view_update(
    WiringView* v,
    const char* port,
    const char* pins,
    const char* baud,
    const char* last_line,
    bool link_up) {
    with_view_model(
        v->view,
        WiringModel * m,
        {
            strncpy(m->port, port, sizeof(m->port) - 1);
            m->port[sizeof(m->port) - 1] = '\0';
            strncpy(m->pins, pins, sizeof(m->pins) - 1);
            m->pins[sizeof(m->pins) - 1] = '\0';
            strncpy(m->baud, baud, sizeof(m->baud) - 1);
            m->baud[sizeof(m->baud) - 1] = '\0';
            strncpy(m->last, last_line, sizeof(m->last) - 1);
            m->last[sizeof(m->last) - 1] = '\0';
            m->link_up = link_up;
        },
        true);
}

void wiring_view_tick(WiringView* v) {
    with_view_model(v->view, WiringModel * m, { m->tick++; }, true);
}
