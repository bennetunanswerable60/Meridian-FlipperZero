#include "learn_view.h"
#include "../helpers/mrd_ui.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/*
 * Six frames on why civil GPS can be lied to, what that costs, and - the frame
 * most tools like this leave out - what this one cannot tell you.
 */

#define ART_TOP 12
#define ART_BOT 40
#define TEXT1_BASE 47
#define TEXT2_BASE 55

struct LearnView {
    View* view;
    LearnViewCallback cb;
    void* ctx;
};

typedef struct {
    uint8_t frame;
    uint32_t tick;
} LearnModel;

static void emit(LearnView* v, MrdViewEvent event) {
    if(v->cb) v->cb(v->ctx, (uint32_t)event);
}

static const char* const TITLES[LEARN_FRAMES] = {
    "Distance from timing",
    "A whisper, unsigned",
    "The loudest wins",
    "Position, and time",
    "What Meridian reads",
    "What it cannot say",
};

static const char* const LINE1[LEARN_FRAMES] = {
    "Four satellites, four",
    "It reaches you weaker than",
    "A local copy, a few dB",
    "Navigation is the obvious",
    "Eleven checks over four",
    "Nothing here is proof. One",
};

static const char* const LINE2[LEARN_FRAMES] = {
    "distances, one point.",
    "the noise. Public. Unsigned.",
    "louder, and it is believed.",
    "loss. Timing is the costly one.",
    "independent paths.",
    "antenna sees tells, not truth.",
};

/* ----------------------------------------------------------------- art -- */

/** Satellites overhead, wavefronts falling to a receiver. */
static void art_trilateration(Canvas* canvas, uint32_t t) {
    const int sx[3] = {24, 64, 104};
    for(int i = 0; i < 3; i++) {
        canvas_draw_box(canvas, sx[i] - 3, ART_TOP + 1, 7, 4);
        canvas_draw_line(canvas, sx[i] - 5, ART_TOP + 3, sx[i] - 4, ART_TOP + 3);
        canvas_draw_line(canvas, sx[i] + 4, ART_TOP + 3, sx[i] + 5, ART_TOP + 3);

        /* Expanding wavefronts, staggered per satellite so the picture reads
         * as three independent ranges rather than one pulse. */
        for(int w = 0; w < 2; w++) {
            int r = (int)((t * 2 + (uint32_t)(i * 5) + (uint32_t)(w * 8)) % 22u);
            if(r > 3) canvas_draw_circle(canvas, sx[i], ART_TOP + 3, r);
        }
    }

    canvas_draw_disc(canvas, 64, ART_BOT - 2, 2);
    canvas_draw_line(canvas, 56, ART_BOT + 1, 72, ART_BOT + 1);
}

/** The signal, buried in the noise it arrives under. */
static void art_noise_floor(Canvas* canvas, uint32_t t) {
    /* A ragged band: the noise the receiver is actually listening through. */
    uint32_t seed = 12345u;
    for(int x = 4; x < 124; x += 2) {
        seed = seed * 1103515245u + 12345u + t / 8u;
        int h = 3 + (int)((seed >> 16) % 7u);
        canvas_draw_line(canvas, x, ART_TOP + 14, x, ART_TOP + 14 - h);
    }
    canvas_draw_line(canvas, 0, ART_TOP + 15, MRD_W - 1, ART_TOP + 15);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, ART_TOP + 24, "noise floor");

    /* And the signal, drawn below it, because that is where it lives. */
    canvas_draw_line(canvas, 96, ART_TOP + 17, 96, ART_TOP + 20);
    canvas_draw_str(canvas, 100, ART_TOP + 24, "GPS");
}

/** The receiver's attention swinging from the sky to a box on the ground. */
static void art_takeover(Canvas* canvas, uint32_t t) {
    canvas_draw_box(canvas, 18, ART_TOP, 7, 4); /* the real satellite, quiet */
    canvas_draw_circle(canvas, 21, ART_TOP + 2, 7);

    /* The spoofer, transmitting hard. */
    canvas_draw_frame(canvas, 96, ART_TOP + 16, 18, 10);
    canvas_draw_line(canvas, 105, ART_TOP + 16, 105, ART_TOP + 10);
    for(int w = 0; w < 3; w++) {
        int r = 4 + (int)((t * 2 + (uint32_t)(w * 6)) % 18u);
        canvas_draw_circle(canvas, 105, ART_TOP + 9, r);
    }

    /* The receiver, and which way it is now looking. */
    canvas_draw_disc(canvas, 58, ART_BOT - 3, 2);
    canvas_draw_line(canvas, 58, ART_BOT - 3, 92, ART_TOP + 12);
    canvas_draw_line(canvas, 88, ART_TOP + 12, 92, ART_TOP + 12);
    canvas_draw_line(canvas, 92, ART_TOP + 12, 92, ART_TOP + 16);
}

/** What actually depends on it. */
static void art_stakes(Canvas* canvas, uint32_t t) {
    UNUSED(t);
    canvas_set_font(canvas, FontSecondary);

    /* A car. */
    canvas_draw_box(canvas, 12, ART_TOP + 12, 18, 5);
    canvas_draw_box(canvas, 16, ART_TOP + 8, 10, 4);
    canvas_draw_disc(canvas, 16, ART_TOP + 18, 2);
    canvas_draw_disc(canvas, 26, ART_TOP + 18, 2);
    canvas_draw_str(canvas, 10, ART_BOT - 1, "cars");

    /* A clock, which is the part people forget. */
    canvas_draw_circle(canvas, 63, ART_TOP + 13, 9);
    canvas_draw_line(canvas, 63, ART_TOP + 13, 63, ART_TOP + 7);
    canvas_draw_line(canvas, 63, ART_TOP + 13, 68, ART_TOP + 15);
    canvas_draw_str(canvas, 52, ART_BOT - 1, "networks");

    /* A mast. */
    canvas_draw_line(canvas, 108, ART_TOP + 20, 108, ART_TOP + 5);
    canvas_draw_line(canvas, 103, ART_TOP + 20, 108, ART_TOP + 8);
    canvas_draw_line(canvas, 113, ART_TOP + 20, 108, ART_TOP + 8);
    canvas_draw_str(canvas, 100, ART_BOT - 1, "the grid");
}

/** The four independent families the engine is organised around. */
static void art_families(Canvas* canvas, uint32_t t) {
    static const char* const NAMES[4] = {"POS", "RF", "GEO", "TIME"};
    canvas_set_font(canvas, FontSecondary);

    for(int i = 0; i < 4; i++) {
        int x = 6 + i * 30;
        bool lit = ((t / 8u) % 4u) == (uint32_t)i;

        if(lit) {
            canvas_draw_rbox(canvas, x, ART_TOP + 6, 26, 16, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, ART_TOP + 6, 26, 16, 3);
        }
        canvas_draw_str_aligned(
            canvas, x + 13, ART_TOP + 17, AlignCenter, AlignBottom, NAMES[i]);
        if(lit) canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_str_aligned(
        canvas, 64, ART_BOT - 1, AlignCenter, AlignBottom, "agreement is the evidence");
}

/** The limit, stated plainly. */
static void art_limits(Canvas* canvas, uint32_t t) {
    mrd_ui_globe(canvas, 26, ART_TOP + 13, 11, t);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 46, ART_TOP + 8, "SUSPECT is as far");
    canvas_draw_str(canvas, 46, ART_TOP + 17, "as one antenna can");
    canvas_draw_str(canvas, 46, ART_TOP + 26, "honestly go alone.");
}

typedef void (*ArtFn)(Canvas*, uint32_t);
static const ArtFn ART[LEARN_FRAMES] = {
    art_trilateration,
    art_noise_floor,
    art_takeover,
    art_stakes,
    art_families,
    art_limits,
};

/* ---------------------------------------------------------------- draw -- */

static void learn_view_draw(Canvas* canvas, void* model) {
    const LearnModel* m = model;
    uint8_t f = m->frame < LEARN_FRAMES ? m->frame : 0;

    canvas_clear(canvas);

    char right[12];
    snprintf(right, sizeof(right), "%u/%u", (unsigned)(f + 1), (unsigned)LEARN_FRAMES);
    mrd_ui_header(canvas, TITLES[f], right);

    ART[f](canvas, m->tick);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, TEXT1_BASE, LINE1[f]);
    canvas_draw_str(canvas, 2, TEXT2_BASE, LINE2[f]);

    mrd_ui_page_dots(canvas, 64, 62, LEARN_FRAMES, f);
}

/* --------------------------------------------------------------- input -- */

static bool learn_view_input(InputEvent* event, void* context) {
    LearnView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyRight) {
        bool last = false;
        with_view_model(
            v->view,
            LearnModel * m,
            {
                if(m->frame + 1 < LEARN_FRAMES) {
                    m->frame++;
                    m->tick = 0;
                } else {
                    last = true;
                }
            },
            true);
        /* Past the last frame, Right leaves rather than sticking: the reader
         * has finished, and making them press Back is a small rudeness. */
        if(last) emit(v, MrdViewEventDone);
        return true;
    }

    if(event->key == InputKeyLeft) {
        with_view_model(
            v->view,
            LearnModel * m,
            {
                if(m->frame > 0) {
                    m->frame--;
                    m->tick = 0;
                }
            },
            true);
        return true;
    }

    return false;
}

/* ---------------------------------------------------------------- glue -- */

LearnView* learn_view_alloc(void) {
    LearnView* v = malloc(sizeof(LearnView));
    memset(v, 0, sizeof(LearnView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(LearnModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, learn_view_draw);
    view_set_input_callback(v->view, learn_view_input);

    return v;
}

void learn_view_free(LearnView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* learn_view_get_view(LearnView* v) {
    return v->view;
}

void learn_view_set_callback(LearnView* v, LearnViewCallback cb, void* context) {
    v->cb = cb;
    v->ctx = context;
}

void learn_view_tick(LearnView* v) {
    with_view_model(v->view, LearnModel * m, { m->tick++; }, true);
}

void learn_view_reset(LearnView* v) {
    with_view_model(
        v->view,
        LearnModel * m,
        {
            m->frame = 0;
            m->tick = 0;
        },
        true);
}
