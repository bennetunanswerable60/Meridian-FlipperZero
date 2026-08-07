#include "../meridian_i.h"

typedef enum {
    SetPort,
    SetBaud,
    SetSensitivity,
    SetHold,
    SetSound,
    SetLed,
    SetCount,
} SettingIndex;

/** Hold windows, in seconds. How long a flag stays up after the condition that
 * raised it has passed. */
static const uint16_t HOLDS[] = {15, 30, 60, 120, 300};
#define HOLD_COUNT (sizeof(HOLDS) / sizeof(HOLDS[0]))

static void port_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.port = i;
    variable_item_set_current_value_text(item, mrd_port_pins(i));
    meridian_apply_settings(app);
}

static void baud_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.baud = i;
    variable_item_set_current_value_text(item, mrd_baud_name(i));
    meridian_apply_settings(app);
}

static void sensitivity_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.sensitivity = i;
    variable_item_set_current_value_text(item, mrd_sensitivity_name(i));
    meridian_apply_settings(app);
}

static void hold_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.hold_s = HOLDS[i];

    char text[12];
    snprintf(text, sizeof(text), "%us", (unsigned)HOLDS[i]);
    variable_item_set_current_value_text(item, text);
}

static void sound_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.sound = (i != 0);
    variable_item_set_current_value_text(item, i ? "On" : "Off");
}

static void led_changed(VariableItem* item) {
    MeridianApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.led = (i != 0);
    variable_item_set_current_value_text(item, i ? "On" : "Off");
}

void meridian_scene_settings_on_enter(void* context) {
    MeridianApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Port", MrdPortCount, port_changed, app);
    variable_item_set_current_value_index(item, app->settings.port);
    variable_item_set_current_value_text(item, mrd_port_pins(app->settings.port));

    item = variable_item_list_add(list, "Baud", MrdBaudCount, baud_changed, app);
    variable_item_set_current_value_index(item, app->settings.baud);
    variable_item_set_current_value_text(item, mrd_baud_name(app->settings.baud));

    item = variable_item_list_add(list, "Sensitivity", 3, sensitivity_changed, app);
    variable_item_set_current_value_index(item, app->settings.sensitivity);
    variable_item_set_current_value_text(item, mrd_sensitivity_name(app->settings.sensitivity));

    uint8_t hold_index = 2;
    for(uint8_t i = 0; i < HOLD_COUNT; i++) {
        if(HOLDS[i] == app->settings.hold_s) hold_index = i;
    }
    item = variable_item_list_add(list, "Alert hold", HOLD_COUNT, hold_changed, app);
    variable_item_set_current_value_index(item, hold_index);
    {
        char text[12];
        snprintf(text, sizeof(text), "%us", (unsigned)HOLDS[hold_index]);
        variable_item_set_current_value_text(item, text);
    }

    item = variable_item_list_add(list, "Sound", 2, sound_changed, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.sound ? "On" : "Off");

    item = variable_item_list_add(list, "LED", 2, led_changed, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.led ? "On" : "Off");

    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, MeridianSceneSettings));

    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewSettings);
}

bool meridian_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meridian_scene_settings_on_exit(void* context) {
    MeridianApp* app = context;

    scene_manager_set_scene_state(
        app->scene_manager,
        MeridianSceneSettings,
        variable_item_list_get_selected_item_index(app->var_item_list));

    /* Written once on the way out rather than on every keypress, so scrolling
     * through the baud rates does not hammer the SD card. */
    meridian_save_settings(app);
    variable_item_list_reset(app->var_item_list);
}
