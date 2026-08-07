#include "../meridian_i.h"

/*
 * One check, explained.
 *
 * The order matters and is deliberate: what was measured, then what a real sky
 * would have done, then - before anything else - what could cause this without
 * an attacker anywhere near you. A detector that lists only the incriminating
 * reading is training its user to over-read it.
 */

void meridian_scene_detail_on_enter(void* context) {
    MeridianApp* app = context;
    Widget* widget = app->widget;

    MrdCheckId id = (MrdCheckId)app->detail_check;
    const MrdCheck* check = &app->snap.det.checks[id];

    char observed[64];
    mrd_check_observed(&app->snap.det, id, observed, sizeof(observed));

    FuriString* text = furi_string_alloc();

    furi_string_cat_printf(text, "\e#%s\e#\n", mrd_check_name(id));
    furi_string_cat_printf(text, "%s\n\n", mrd_check_title(id));

    furi_string_cat_printf(text, "\e#Right now\e#\n");
    furi_string_cat_printf(text, "State: %s\n", mrd_state_name((MrdCheckState)check->state));
    furi_string_cat_printf(text, "Reading: %s\n", observed);
    if(check->hits > 0) {
        furi_string_cat_printf(text, "Flagged in %u epochs\n", (unsigned)check->hits);
    } else if(check->armed) {
        furi_string_cat_printf(text, "Never flagged this session\n");
    }
    furi_string_cat_printf(text, "\n");

    furi_string_cat_printf(text, "\e#What a real sky does\e#\n%s\n\n", mrd_check_what(id));
    furi_string_cat_printf(text, "\e#Innocent explanations\e#\n%s\n\n", mrd_check_benign(id));

    furi_string_cat_printf(
        text,
        "\e#Remember\e#\n"
        "One check is never proof. Meridian only reaches its top verdict when "
        "several independent measurement paths agree, and even then it says "
        "likely rather than certain.\n");

    widget_reset(widget);
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewText);
}

bool meridian_scene_detail_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meridian_scene_detail_on_exit(void* context) {
    MeridianApp* app = context;
    widget_reset(app->widget);
}
