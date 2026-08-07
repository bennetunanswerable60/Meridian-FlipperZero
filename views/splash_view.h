#pragma once

#include <gui/view.h>
#include <stdint.h>

#include "mrd_view_event.h"

typedef struct SplashView SplashView;

typedef void (*SplashViewCallback)(void* context, uint32_t event);

SplashView* splash_view_alloc(void);
void splash_view_free(SplashView* view);
View* splash_view_get_view(SplashView* view);

void splash_view_set_callback(SplashView* view, SplashViewCallback cb, void* context);

/** Drive the animation. The scene owns the timing and decides when to move on. */
void splash_view_set_phase(SplashView* view, uint32_t phase);
