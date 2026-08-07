#pragma once

#include <gui/view.h>

#include "../helpers/mrd_gps.h"
#include "mrd_view_event.h"

typedef struct MonitorView MonitorView;

typedef void (*MonitorViewCallback)(void* context, uint32_t event);

MonitorView* monitor_view_alloc(void);
void monitor_view_free(MonitorView* view);
View* monitor_view_get_view(MonitorView* view);

void monitor_view_set_callback(MonitorView* view, MonitorViewCallback cb, void* context);

/** Publish one epoch. @p hint keeps the control legend up. */
void monitor_view_update(
    MonitorView* view,
    const MrdSnapshot* snap,
    uint32_t elapsed_s,
    bool hint);
