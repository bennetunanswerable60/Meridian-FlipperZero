#pragma once

#include <gui/view.h>
#include <stdint.h>

#include "mrd_view_event.h"

#define LEARN_FRAMES 6

typedef struct LearnView LearnView;

typedef void (*LearnViewCallback)(void* context, uint32_t event);

LearnView* learn_view_alloc(void);
void learn_view_free(LearnView* view);
View* learn_view_get_view(LearnView* view);

void learn_view_set_callback(LearnView* view, LearnViewCallback cb, void* context);

/** Advance the animation within the current frame. */
void learn_view_tick(LearnView* view);
void learn_view_reset(LearnView* view);
