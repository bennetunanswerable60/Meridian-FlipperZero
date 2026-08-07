#include "../meridian_i.h"

/*
 * Demo mode exists for two reasons, and only one of them is that most people
 * do not own a GPS module.
 *
 * The other is that a detector should be watchable while it is being attacked.
 * Every scenario here drives the real parser and the real engine, so what you
 * see the checks do on the bench is exactly what they will do in the field -
 * including the two scenarios designed to demonstrate its limits: a clean sky
 * that must stay quiet, and a jammer that must not be called a spoofer.
 */

static void demo_cb(void* context, uint32_t index) {
    MeridianApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meridian_scene_demo_on_enter(void* context) {
    MeridianApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Simulated receiver");

    for(uint8_t i = 0; i < MrdScenarioCount; i++) {
        submenu_add_item(menu, mrd_sim_name((MrdScenario)i), i, demo_cb, app);
    }

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MeridianSceneDemo));

    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewSubmenu);
}

bool meridian_scene_demo_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event < MrdScenarioCount) {
        scene_manager_set_scene_state(app->scene_manager, MeridianSceneDemo, event.event);
        app->demo_pending = true;
        app->demo_scenario = (uint8_t)event.event;
        scene_manager_next_scene(app->scene_manager, MeridianSceneMonitor);
        return true;
    }
    return false;
}

void meridian_scene_demo_on_exit(void* context) {
    MeridianApp* app = context;
    submenu_reset(app->submenu);
}
