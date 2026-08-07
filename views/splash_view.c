#include "splash_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <string.h>

struct SplashView {
    View* view;
    SplashViewCallback cb;
    void* ctx;
};

typedef struct {
    uint32_t phase;
} SplashModel;

/**
 * The mark: a globe with its meridian turning under a line that does not move.
 * Which is the whole conceit - the meridian is the fixed reference, and this
 * application exists because something may be lying to you about where you
 * stand relative to it.
 */
static void splash_view_draw(Canvas* canvas, void* model) {
    const SplashModel* m = model;

    canvas_clear(canvas);

    mrd_ui_globe(canvas, 28, 32, 19, m->phase);

    /* The fixed meridian: a vertical rule the globe turns beneath. */
    canvas_draw_line(canvas, 28, 6, 28, 58);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 58, 30, "MERIDIAN");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 58, 41, "GPS integrity");
    canvas_draw_str(canvas, 58, 50, "monitor");
}

static bool splash_view_input(InputEvent* event, void* context) {
    SplashView* v = context;
    /* Any press skips ahead. A splash that cannot be dismissed is a nuisance
     * the second time you open the app. */
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        if(v->cb) v->cb(v->ctx, (uint32_t)MrdViewEventDone);
        return true;
    }
    return false;
}

SplashView* splash_view_alloc(void) {
    SplashView* v = malloc(sizeof(SplashView));
    memset(v, 0, sizeof(SplashView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SplashModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, splash_view_draw);
    view_set_input_callback(v->view, splash_view_input);

    return v;
}

void splash_view_free(SplashView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* splash_view_get_view(SplashView* v) {
    return v->view;
}

void splash_view_set_callback(SplashView* v, SplashViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void splash_view_set_phase(SplashView* v, uint32_t phase) {
    with_view_model(v->view, SplashModel * m, { m->phase = phase; }, true);
}
