/**
 * Meridian - is anything lying to you about where you are?
 *
 * Civil GPS is a public, unsigned, unencrypted signal that arrives below the
 * thermal noise floor. Anything willing to transmit the same structure a few
 * decibels louder will be believed, completely and without complaint, and the
 * receiver will keep reporting a confident position the entire time.
 *
 * There is no test for "is this real". What there is, is a list of things a
 * genuine sky does that a transmitter has to work to imitate: power that rises
 * with elevation, geometry that drifts minute by minute, a fix that wanders a
 * metre or two because it is being computed rather than recited, and a clock
 * that advances at one second per second. Meridian watches eleven of those
 * across four independent measurement paths, scores the disagreement, and shows
 * its working.
 *
 * It reads NMEA from a GPS module on the GPIO header. It never transmits.
 */
#include "meridian_i.h"

static bool meridian_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    MeridianApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool meridian_back_event_callback(void* context) {
    furi_assert(context);
    MeridianApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/** One timer for the whole app; every animated scene starts and stops it. */
static void meridian_tick_callback(void* context) {
    MeridianApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MrdEventTick);
}

/* ------------------------------------------------------------- helpers -- */

void meridian_apply_settings(MeridianApp* app) {
    furi_assert(app);
    mrd_gps_configure(app->gps, &app->settings);
}

void meridian_save_settings(MeridianApp* app) {
    furi_assert(app);
    mrd_settings_save(&app->settings);
}

void meridian_refresh(MeridianApp* app) {
    furi_assert(app);

    mrd_gps_snapshot(app->gps, &app->snap);

    uint32_t now = furi_get_tick();
    uint32_t elapsed_s = (now - app->started_tick) / furi_kernel_get_tick_frequency();
    bool hint = (int32_t)(app->hint_until - now) > 0;

    /* Only the visible view is updated. Copying a snapshot into four view
     * models ten times a second would be pure waste, and the others are
     * refreshed the moment they come on screen anyway. */
    switch(app->page) {
    case MeridianPageMonitor:
        monitor_view_update(app->monitor_view, &app->snap, elapsed_s, hint);
        break;
    case MeridianPageSky:
        sky_view_update(app->sky_view, &app->snap);
        break;
    case MeridianPageTrail:
        trail_view_update(app->trail_view, &app->snap);
        break;
    case MeridianPageEvidence:
        evidence_view_update(app->evidence_view, &app->snap);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- notify -- */

void meridian_notify_alert(MeridianApp* app) {
    uint32_t now = furi_get_tick();
    if(now - app->last_alert_tick < furi_ms_to_ticks(MERIDIAN_ALERT_GAP_MS)) return;
    app->last_alert_tick = now;

    if(app->settings.led) notification_message(app->notifications, &sequence_blink_red_100);
    if(app->settings.sound) notification_message(app->notifications, &sequence_error);
}

void meridian_notify_jamming(MeridianApp* app) {
    uint32_t now = furi_get_tick();
    if(now - app->last_alert_tick < furi_ms_to_ticks(MERIDIAN_ALERT_GAP_MS)) return;
    app->last_alert_tick = now;

    if(app->settings.led) notification_message(app->notifications, &sequence_blink_blue_100);
    if(app->settings.sound) notification_message(app->notifications, &sequence_double_vibro);
}

void meridian_notify_click(MeridianApp* app) {
    if(app->settings.sound) notification_message(app->notifications, &sequence_semi_success);
}

/* ----------------------------------------------------------- lifecycle -- */

static MeridianApp* meridian_app_alloc(void) {
    MeridianApp* app = malloc(sizeof(MeridianApp));
    memset(app, 0, sizeof(MeridianApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&meridian_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, meridian_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, meridian_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        MeridianViewSettings,
        variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewText, widget_get_view(app->widget));

    app->splash_view = splash_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewSplash, splash_view_get_view(app->splash_view));

    app->monitor_view = monitor_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewMonitor, monitor_view_get_view(app->monitor_view));

    app->sky_view = sky_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewSky, sky_view_get_view(app->sky_view));

    app->trail_view = trail_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewTrail, trail_view_get_view(app->trail_view));

    app->evidence_view = evidence_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewEvidence, evidence_view_get_view(app->evidence_view));

    app->learn_view = learn_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewLearn, learn_view_get_view(app->learn_view));

    app->wiring_view = wiring_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MeridianViewWiring, wiring_view_get_view(app->wiring_view));

    mrd_settings_load(&app->settings);

    app->gps = mrd_gps_alloc(app->view_dispatcher, MrdEventEpoch);
    meridian_apply_settings(app);

    app->timer = furi_timer_alloc(meridian_tick_callback, FuriTimerTypePeriodic, app);

    return app;
}

static void meridian_app_free(MeridianApp* app) {
    furi_assert(app);

    /* The source owns a thread that writes into state the views read, so it
     * goes down and is joined before anything it touches is released. */
    mrd_gps_free(app->gps);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewText);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewSplash);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewMonitor);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewSky);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewTrail);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewEvidence);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewLearn);
    view_dispatcher_remove_view(app->view_dispatcher, MeridianViewWiring);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    splash_view_free(app->splash_view);
    monitor_view_free(app->monitor_view);
    sky_view_free(app->sky_view);
    trail_view_free(app->trail_view);
    evidence_view_free(app->evidence_view);
    learn_view_free(app->learn_view);
    wiring_view_free(app->wiring_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t meridian_app(void* p) {
    UNUSED(p);

    MeridianApp* app = meridian_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, MeridianSceneSplash);
    view_dispatcher_run(app->view_dispatcher);

    meridian_app_free(app);
    return 0;
}
