#include "../meridian_i.h"

static void learn_cb(void* context, uint32_t event) {
    MeridianApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MrdEventViewBase + event);
}

void meridian_scene_learn_on_enter(void* context) {
    MeridianApp* app = context;

    learn_view_set_callback(app->learn_view, learn_cb, app);
    learn_view_reset(app->learn_view);

    furi_timer_start(app->timer, furi_ms_to_ticks(MERIDIAN_TICK_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewLearn);
}

bool meridian_scene_learn_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MrdEventTick) {
            learn_view_tick(app->learn_view);
            return true;
        }
        if(event.event == MrdEventViewBase + MrdViewEventDone) {
            /* Right off the end of the last frame leaves, rather than sitting
             * there waiting for a Back press the reader has not earned. */
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
    }
    return false;
}

void meridian_scene_learn_on_exit(void* context) {
    MeridianApp* app = context;
    furi_timer_stop(app->timer);
}
