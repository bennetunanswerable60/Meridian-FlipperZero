#pragma once

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

#include "mrd_view_event.h"

typedef struct WiringView WiringView;

WiringView* wiring_view_alloc(void);
void wiring_view_free(WiringView* view);
View* wiring_view_get_view(WiringView* view);

/**
 * @p last_line is whatever sentence arrived most recently, or "" if nothing
 * has. Showing the real bytes is the fastest way to tell a wiring fault from a
 * baud-rate fault: the wrong baud produces mojibake, a swapped pair produces
 * silence.
 */
void wiring_view_update(
    WiringView* view,
    const char* port,
    const char* pins,
    const char* baud,
    const char* last_line,
    bool link_up);

void wiring_view_tick(WiringView* view);
