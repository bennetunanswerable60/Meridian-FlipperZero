#pragma once

#include <gui/view.h>

#include "../helpers/mrd_gps.h"
#include "mrd_view_event.h"

typedef struct SkyView SkyView;

typedef void (*SkyViewCallback)(void* context, uint32_t event);

SkyView* sky_view_alloc(void);
void sky_view_free(SkyView* view);
View* sky_view_get_view(SkyView* view);

void sky_view_set_callback(SkyView* view, SkyViewCallback cb, void* context);
void sky_view_update(SkyView* view, const MrdSnapshot* snap);
