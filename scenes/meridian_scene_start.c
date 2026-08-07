#include "../meridian_i.h"

typedef enum {
    StartMonitor,
    StartDemo,
    StartLearn,
    StartWiring,
    StartSettings,
    StartAbout,
} StartIndex;

static void start_cb(void* context, uint32_t index) {
    MeridianApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meridian_scene_start_on_enter(void* context) {
    MeridianApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Meridian");

    submenu_add_item(menu, "Monitor GPS", StartMonitor, start_cb, app);
    submenu_add_item(menu, "Demo without hardware", StartDemo, start_cb, app);
    submenu_add_item(menu, "How spoofing works", StartLearn, start_cb, app);
    submenu_add_item(menu, "Wiring", StartWiring, start_cb, app);
    submenu_add_item(menu, "Settings", StartSettings, start_cb, app);
    submenu_add_item(menu, "About", StartAbout, start_cb, app);

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MeridianSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewSubmenu);
}

bool meridian_scene_start_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, MeridianSceneStart, event.event);

        switch(event.event) {
        case StartMonitor:
            app->demo_pending = false;
            scene_manager_next_scene(app->scene_manager, MeridianSceneMonitor);
            return true;
        case StartDemo:
            scene_manager_next_scene(app->scene_manager, MeridianSceneDemo);
            return true;
        case StartLearn:
            scene_manager_next_scene(app->scene_manager, MeridianSceneLearn);
            return true;
        case StartWiring:
            scene_manager_next_scene(app->scene_manager, MeridianSceneWiring);
            return true;
        case StartSettings:
            scene_manager_next_scene(app->scene_manager, MeridianSceneSettings);
            return true;
        case StartAbout:
            scene_manager_next_scene(app->scene_manager, MeridianSceneAbout);
            return true;
        default:
            break;
        }
    }
    return false;
}

void meridian_scene_start_on_exit(void* context) {
    MeridianApp* app = context;
    submenu_reset(app->submenu);
}
