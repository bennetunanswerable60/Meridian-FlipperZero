#include "../meridian_i.h"

/*
 * The hub. One scene owns all four live screens, because they are four windows
 * onto one running session rather than four places to go: Left and Right walk
 * between them without ever stopping the receiver or losing the history.
 */

/** Set before pushing the detail card, so on_exit can tell "the user is
 * leaving" from "the scene manager is running on_exit because a child scene is
 * being pushed on top of us". Without this the receiver would be torn down and
 * restarted every time somebody read a check. */
#define MON_STATE_DETOUR 1

static const MeridianViewId PAGE_VIEW[MeridianPageCount] = {
    MeridianViewMonitor,
    MeridianViewSky,
    MeridianViewTrail,
    MeridianViewEvidence,
};

static void view_cb(void* context, uint32_t event) {
    MeridianApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MrdEventViewBase + event);
}

static void show_page(MeridianApp* app, uint8_t page) {
    app->page = page;
    /* Fill the incoming view before it is shown, so the page swap never
     * flashes a frame of whatever it was holding last time. */
    meridian_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PAGE_VIEW[page]);
}

static void step_page(MeridianApp* app, int delta) {
    int page = (int)app->page + delta;
    if(page < 0) page = MeridianPageCount - 1;
    if(page >= MeridianPageCount) page = 0;
    meridian_notify_click(app);
    show_page(app, (uint8_t)page);
}

/**
 * Say something once, when the situation gets worse - never on every epoch.
 * An integrity monitor that buzzes continuously gets muted, and a muted monitor
 * is not a monitor.
 */
static void check_for_news(MeridianApp* app) {
    uint8_t verdict = app->snap.det.verdict;

    if(verdict > app->last_verdict && verdict >= MrdVerdictSuspect) {
        meridian_notify_alert(app);
    }
    app->last_verdict = verdict;

    if(app->snap.det.jamming && !app->was_jamming) meridian_notify_jamming(app);
    app->was_jamming = app->snap.det.jamming;
}

void meridian_scene_monitor_on_enter(void* context) {
    MeridianApp* app = context;

    monitor_view_set_callback(app->monitor_view, view_cb, app);
    sky_view_set_callback(app->sky_view, view_cb, app);
    trail_view_set_callback(app->trail_view, view_cb, app);
    evidence_view_set_callback(app->evidence_view, view_cb, app);

    /* Coming back from the detail card: the session is still running and the
     * page the user was on is still the page they want. */
    bool returning = mrd_gps_is_running(app->gps);

    if(!returning) {
        app->started_tick = furi_get_tick();
        app->hint_until = furi_get_tick() + furi_ms_to_ticks(MERIDIAN_HINT_MS);
        app->last_verdict = MrdVerdictNoSignal;
        app->was_jamming = false;
        app->page = MeridianPageMonitor;

        meridian_apply_settings(app);
        if(app->demo_pending) {
            mrd_gps_start_demo(app->gps, (MrdScenario)app->demo_scenario);
        } else {
            mrd_gps_start_live(app->gps);
        }
    }

    scene_manager_set_scene_state(app->scene_manager, MeridianSceneMonitor, 0);
    furi_timer_start(app->timer, furi_ms_to_ticks(MERIDIAN_TICK_MS));
    show_page(app, app->page);
}

bool meridian_scene_monitor_on_event(void* context, SceneManagerEvent event) {
    MeridianApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == MrdEventTick) {
        meridian_refresh(app);
        return true;
    }

    if(event.event == MrdEventEpoch) {
        meridian_refresh(app);
        check_for_news(app);
        return true;
    }

    if(event.event >= MrdEventViewBase) {
        switch(event.event - MrdEventViewBase) {
        case MrdViewEventPageNext:
            step_page(app, 1);
            return true;

        case MrdViewEventPagePrev:
            step_page(app, -1);
            return true;

        case MrdViewEventDetail:
            if(app->page == MeridianPageEvidence) {
                /* Read the check the cursor is on. */
                app->detail_check = evidence_view_selected(app->evidence_view);
                scene_manager_set_scene_state(
                    app->scene_manager, MeridianSceneMonitor, MON_STATE_DETOUR);
                scene_manager_next_scene(app->scene_manager, MeridianSceneDetail);
            } else {
                /* From anywhere else, OK means "show me why", which is the
                 * evidence list rather than a card about one check. */
                show_page(app, MeridianPageEvidence);
                meridian_notify_click(app);
            }
            return true;

        default:
            break;
        }
    }

    return false;
}

void meridian_scene_monitor_on_exit(void* context) {
    MeridianApp* app = context;

    furi_timer_stop(app->timer);

    /* Pushing a child scene runs this handler too. Only a real exit takes the
     * receiver down; a detour leaves the session, and the whole history behind
     * the verdict, exactly where it was. */
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, MeridianSceneMonitor);
    if(state != MON_STATE_DETOUR) {
        mrd_gps_stop(app->gps);
        app->demo_pending = false;
    }
}
