#pragma once

#include <gui/view.h>

#include "../helpers/mrd_gps.h"
#include "mrd_view_event.h"

typedef struct TrailView TrailView;

typedef void (*TrailViewCallback)(void* context, uint32_t event);

TrailView* trail_view_alloc(void);
void trail_view_free(TrailView* view);
View* trail_view_get_view(TrailView* view);

void trail_view_set_callback(TrailView* view, TrailViewCallback cb, void* context);
void trail_view_update(TrailView* view, const MrdSnapshot* snap);
