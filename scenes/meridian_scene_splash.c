#include "../meridian_i.h"

/*
 * The mark, for a second and a half, skippable with any key. It also covers the
 * settings load off the SD card, so it costs nothing in practice.
 */

typedef struct {
    uint32_t phase;
} SplashState;

static void splash_cb(void* context, uint32_t event) {
    MeridianApp* app = context;
    if(event == MrdViewEventDone) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MrdEventViewBase + event);
    }
}

/** on_enter must not navigate, so leaving is always posted as an event and
 * unwound on the next dispatch. */
static void leave(MeridianApp* app) {
    scene_manager_next_scene(app->scene_manager, MeridianSceneStart);
}

void meridian_scene_splash_on_enter(void* context) {
    MeridianApp* app = context;

    splash_view_set_callback(app->splash_view, splash_cb, app);
    splash_view_set_phase(app->splash_view, 0);
    scene_manager_set_scene_state(app->scene_manager, MeridianSceneSplash, 0);

    furi_timer_start(app->timer, furi_ms_to_ticks(MERIDIAN_TICK_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewSplash);
}

bool meridian_scene_splash_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MrdEventTick) {
            uint32_t phase =
                scene_manager_get_scene_state(app->scene_manager, MeridianSceneSplash) + 1;
            scene_manager_set_scene_state(app->scene_manager, MeridianSceneSplash, phase);
            splash_view_set_phase(app->splash_view, phase);

            if(phase * MERIDIAN_TICK_MS >= MERIDIAN_SPLASH_MS) leave(app);
            return true;
        }
        if(event.event == MrdEventViewBase + MrdViewEventDone) {
            leave(app);
            return true;
        }
    }
    return false;
}

void meridian_scene_splash_on_exit(void* context) {
    MeridianApp* app = context;
    furi_timer_stop(app->timer);
}
