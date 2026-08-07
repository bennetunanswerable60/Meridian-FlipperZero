#pragma once

/*
 * Where the sentences come from.
 *
 * Two sources, one pipeline. Live mode reads a NMEA module on the Flipper's
 * GPIO UART; demo mode runs the built-in simulator. Both hand identical text
 * to the same parser and the same engine, so demo mode is not a slideshow of
 * canned screens - it is the real detector working on a synthetic sky. What
 * you see it do on the bench is what it will do in the field.
 *
 * A worker thread owns the parser and the engine outright. Every epoch it
 * publishes a snapshot under a mutex and pokes the GUI with a custom event, so
 * nothing on the drawing side ever touches state that is moving.
 */

#include <furi.h>
#include <gui/view_dispatcher.h>

#include "mrd_detect.h"
#include "mrd_nmea.h"
#include "mrd_settings.h"
#include "mrd_sim.h"

/** Wall-clock milliseconds between simulated seconds. Demo runs at 4x so the
 * slower checks - four minutes of sky motion - land inside a demonstration. */
#define MRD_DEMO_STEP_MS 250

/** No sentences for this long and the link is reported down. */
#define MRD_LINK_TIMEOUT_MS 3000

/** Longest sentence kept for the wiring screen's link readout. */
#define MRD_LAST_LINE 44

/** Everything the screens are allowed to look at. Copied whole under the
 * source's lock, so a view never reads a half-updated epoch. */
typedef struct {
    MrdFix fix;
    MrdSat sats[MRD_MAX_SATS];
    uint8_t sat_count;
    MrdDetect det;
    MrdTrail trail;

    uint32_t sentences;
    uint32_t bad_checksum;
    uint32_t epochs;
    bool link_up;
    bool demo;
    uint8_t scenario;
    char last_line[MRD_LAST_LINE];
} MrdSnapshot;

typedef struct MrdGps MrdGps;

MrdGps* mrd_gps_alloc(ViewDispatcher* dispatcher, uint32_t epoch_event);
void mrd_gps_free(MrdGps* gps);

/** Apply settings. Baud and port take effect on the next start. */
void mrd_gps_configure(MrdGps* gps, const MrdSettings* settings);

/** Start reading a real receiver. */
void mrd_gps_start_live(MrdGps* gps);

/** Start the simulator on @p scenario instead. */
void mrd_gps_start_demo(MrdGps* gps, MrdScenario scenario);

void mrd_gps_stop(MrdGps* gps);
bool mrd_gps_is_running(MrdGps* gps);

/** Copy out the latest published epoch. */
void mrd_gps_snapshot(MrdGps* gps, MrdSnapshot* out);

/** Clear the session: history, latched flags, the trail, the counters. */
void mrd_gps_reset(MrdGps* gps);
