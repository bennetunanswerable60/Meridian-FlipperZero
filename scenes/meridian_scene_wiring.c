#include "../meridian_i.h"

/*
 * Reachable from the menu without starting anything, because the moment you
 * need it is before the first run. If a session happens to be up, the last
 * sentence received is shown underneath - which turns "is it plugged in right"
 * from a guess into a look.
 */

static void push(MeridianApp* app) {
    mrd_gps_snapshot(app->gps, &app->snap);

    wiring_view_update(
        app->wiring_view,
        mrd_port_name(app->settings.port),
        mrd_port_pins(app->settings.port),
        mrd_baud_name(app->settings.baud),
        app->snap.last_line,
        app->snap.link_up);
}

void meridian_scene_wiring_on_enter(void* context) {
    MeridianApp* app = context;

    push(app);
    furi_timer_start(app->timer, furi_ms_to_ticks(MERIDIAN_TICK_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewWiring);
}

bool meridian_scene_wiring_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == MrdEventTick) {
        wiring_view_tick(app->wiring_view);
        push(app);
        return true;
    }
    return false;
}

void meridian_scene_wiring_on_exit(void* context) {
    MeridianApp* app = context;
    furi_timer_stop(app->timer);
}
