#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "meridian_icons.h" // generated from icons/ by fbt

#include "helpers/mrd_detect.h"
#include "helpers/mrd_gps.h"
#include "helpers/mrd_nmea.h"
#include "helpers/mrd_settings.h"
#include "helpers/mrd_sim.h"
#include "helpers/mrd_ui.h"

#include "views/evidence_view.h"
#include "views/learn_view.h"
#include "views/monitor_view.h"
#include "views/mrd_view_event.h"
#include "views/sky_view.h"
#include "views/splash_view.h"
#include "views/trail_view.h"
#include "views/wiring_view.h"

#include "scenes/meridian_scene.h"

#define MERIDIAN_VERSION "1.0"

/** Redraw cadence. Fast enough for the animations, slow enough to leave the
 * UART worker plenty of room. */
#define MERIDIAN_TICK_MS 100

/** How long the control legend stays up when a live screen opens. */
#define MERIDIAN_HINT_MS 3000

/** Splash duration. Long enough to read, short enough not to be in the way. */
#define MERIDIAN_SPLASH_MS 1600

/** Floor on the gap between two alert chirps. */
#define MERIDIAN_ALERT_GAP_MS 4000

typedef enum {
    MeridianViewSubmenu,
    MeridianViewSettings,
    MeridianViewText,
    MeridianViewSplash,
    MeridianViewMonitor,
    MeridianViewSky,
    MeridianViewTrail,
    MeridianViewEvidence,
    MeridianViewLearn,
    MeridianViewWiring,
} MeridianViewId;

/** The live screens, in the order Left and Right walk them. */
typedef enum {
    MeridianPageMonitor = 0,
    MeridianPageSky,
    MeridianPageTrail,
    MeridianPageEvidence,
    MeridianPageCount,
} MeridianPage;

_Static_assert(MeridianPageCount == MRD_PAGE_COUNT, "page indicator must match the page cycle");

typedef enum {
    /** Periodic redraw, posted by the app timer. */
    MrdEventTick = 100,
    /** The worker finished an epoch and published it. */
    MrdEventEpoch,
    /** View events arrive offset from here, so a scene decodes one with a
     * single subtraction - the same way the rest of this family does it. */
    MrdEventViewBase = 200,
} MeridianCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    SplashView* splash_view;
    MonitorView* monitor_view;
    SkyView* sky_view;
    TrailView* trail_view;
    EvidenceView* evidence_view;
    LearnView* learn_view;
    WiringView* wiring_view;

    MrdGps* gps;
    MrdSettings settings;

    /** The most recent published epoch, copied on the GUI thread. Everything
     * on the drawing side reads this and never the worker's own state. */
    MrdSnapshot snap;

    FuriTimer* timer;

    uint8_t page; /**< MeridianPage */
    uint8_t detail_check; /**< which check the detail card is showing */
    bool demo_pending; /**< the monitor scene should start the simulator */
    uint8_t demo_scenario;

    uint32_t started_tick;
    uint32_t hint_until;
    uint32_t last_alert_tick;
    uint8_t last_verdict; /**< so a worsening verdict can be noticed once */
    bool was_jamming;
} MeridianApp;

/** Pull a fresh snapshot and hand it to whichever live view is on screen. */
void meridian_refresh(MeridianApp* app);

/** Push settings into the source. */
void meridian_apply_settings(MeridianApp* app);
void meridian_save_settings(MeridianApp* app);

/* feedback, gated by settings */
void meridian_notify_alert(MeridianApp* app);
void meridian_notify_jamming(MeridianApp* app);
void meridian_notify_click(MeridianApp* app);
