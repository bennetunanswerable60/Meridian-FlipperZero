#pragma once

/*
 * The integrity engine.
 *
 * A civil GPS receiver has no way to authenticate what it hears. The L1 C/A
 * signal is public, unencrypted and unsigned, and it arrives at about
 * -130 dBm, which is below the thermal noise floor. Anything that transmits
 * the same structure a few dB louder wins, and the receiver reports the
 * spoofer's answer with no complaint at all.
 *
 * So there is no test for "is this real". What there is, is a set of things a
 * genuine sky does that a transmitter has to work quite hard to imitate:
 * satellites at different elevations arrive at different powers, the geometry
 * drifts minute by minute, the position wanders by a metre or two even when
 * bolted to a wall, and the clock advances at exactly one second per second.
 * Each check below tests one of those, reports what it saw, and says what a
 * benign cause would be.
 *
 * Nothing here calls into the Flipper API, so the whole engine runs on the
 * host under test/host_detect_test.c against synthesised clean and spoofed
 * streams. That matters more than usual: this is a detector, and a detector
 * you have not tried to fool is a decoration.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mrd_nmea.h"

/** Positions kept for the drift/trail plot. */
#define MRD_TRAIL_LEN 64

/** Satellites remembered for the sky-motion snapshot. */
#define MRD_SKY_SNAP 20

/** Epochs in the frozen-position window. At 1 Hz this is 16 seconds. */
#define MRD_FROZEN_WIN 16

typedef enum {
    /** Position moved further than any vehicle could in the time available. */
    MrdCheckJump = 0,
    /** Doppler speed and position-derived speed disagree. */
    MrdCheckVelocity,
    /** Carrier powers are too alike across the constellation. */
    MrdCheckSnrFlat,
    /** Carrier powers are higher than open sky delivers. */
    MrdCheckSnrHot,
    /** Power no longer rises with elevation. */
    MrdCheckSnrElev,
    /** Receiver time is not advancing with the local clock. */
    MrdCheckClock,
    /** Position is bit-identical epoch after epoch. */
    MrdCheckFrozen,
    /** Satellite geometry has not moved in minutes. */
    MrdCheckSkyStatic,
    /** Claimed accuracy is better than the geometry can support, or canned. */
    MrdCheckDop,
    /** Altitude jumped, or is pinned to a constant. */
    MrdCheckAltitude,
    /** Lock was lost and regained somewhere else entirely. */
    MrdCheckCapture,

    MrdCheckCount,
} MrdCheckId;

typedef enum {
    MrdStateIdle = 0, /**< not enough data yet — this check has not run */
    MrdStateOk,
    MrdStateWarn,
    MrdStateAlert,
} MrdCheckState;

typedef struct {
    uint8_t state; /**< MrdCheckState */
    bool armed; /**< has ever had enough data to produce a verdict */
    float value; /**< the number this check is about, for display */
    uint16_t hits; /**< epochs in which it flagged, this session */
    uint32_t last_epoch; /**< epoch index of the most recent flag */
    uint32_t hold_until; /**< a flag latches until this epoch */
} MrdCheck;

typedef enum {
    MrdVerdictNoSignal = 0, /**< nothing decoded yet */
    MrdVerdictWarmup, /**< too few checks armed to say anything */
    MrdVerdictNominal, /**< no tells */
    MrdVerdictAnomalous, /**< something is off, benign causes are likely */
    MrdVerdictSuspect, /**< several independent tells agree */
    MrdVerdictLikely, /**< strong, consistent evidence of a fake sky */
    MrdVerdictCount,
} MrdVerdict;

typedef enum {
    MrdSensLow = 0,
    MrdSensNormal,
    MrdSensHigh,
} MrdSensitivity;

typedef struct {
    double lat, lon;
} MrdTrailPoint;

/**
 * Recent fixes, for the drift plot.
 *
 * Display state rather than detection state, so it lives out here where the
 * screens that want it can copy it and the ones that do not are not carrying a
 * kilobyte of coordinates around in their view model.
 */
typedef struct {
    MrdTrailPoint pt[MRD_TRAIL_LEN];
    uint8_t count;
    uint8_t head;
} MrdTrail;

void mrd_trail_push(MrdTrail* t, double lat, double lon);

/** Oldest-first indexing, so a plot can walk it in time order. */
const MrdTrailPoint* mrd_trail_at(const MrdTrail* t, uint8_t index);

typedef struct {
    MrdCheck checks[MrdCheckCount];

    /* ---- published state, safe for a view to read ---- */
    uint8_t verdict; /**< MrdVerdict */
    uint8_t score; /**< 0..100 confidence that the sky is not real */
    uint8_t armed; /**< checks with enough data to have run */
    uint8_t alerts; /**< checks currently at ALERT */
    uint8_t warns; /**< checks currently at WARN */
    uint8_t families; /**< independent measurement paths currently alerting, 0..4 */

    bool jamming; /**< denial, as opposed to deception */
    uint8_t jam_reason;

    /* ---- live measurements the screens display ---- */
    uint8_t sats_tracked; /**< reporting a non-zero C/N0 */
    float snr_mean;
    float snr_sigma;
    float snr_elev_r; /**< Pearson r of elevation against C/N0 */
    float speed_derived; /**< m/s implied by consecutive positions */
    float clock_drift_s; /**< receiver time minus local elapsed time */
    uint32_t epoch; /**< epochs processed this session */
    uint32_t epochs_valid; /**< epochs that carried a usable fix */

    /* ---- internals ---- */
    MrdSensitivity sens;
    uint32_t hold_epochs;

    bool prev_valid;
    double prev_lat, prev_lon;
    float prev_alt;
    uint32_t prev_utc_ms;
    uint32_t prev_mono_ms;

    bool have_last_good;
    double last_good_lat, last_good_lon;
    uint32_t last_good_mono_ms;

    uint32_t frozen_bits; /**< one bit per epoch: position identical to previous */
    uint8_t frozen_filled;

    float last_hdop;
    uint8_t hdop_same_run;
    uint8_t alt_zero_run;
    uint8_t velo_run;
    uint8_t nofix_run;

    /* sky-motion snapshot */
    struct {
        uint8_t sys, prn, elev;
    } sky_snap[MRD_SKY_SNAP];
    uint8_t sky_snap_count;
    uint32_t sky_snap_mono_ms;

    int64_t drift_accum_ms; /**< signed, receiver clock against local clock */
} MrdDetect;

/** Reasons the jamming indicator can be up. */
enum {
    MrdJamNone = 0,
    MrdJamNoSolution, /**< satellites in view, none usable */
    MrdJamFloorUp, /**< carrier powers collapsed together */
    MrdJamLostFix, /**< fix dropped with the sky still in view */
};

void mrd_detect_init(MrdDetect* d, MrdSensitivity sens, uint16_t hold_seconds);

/** Wipe the session: measurements, history and every latched flag. */
void mrd_detect_reset(MrdDetect* d);

/** Change sensitivity without losing the session. */
void mrd_detect_set_sensitivity(MrdDetect* d, MrdSensitivity sens);

/**
 * Run every check over one complete epoch.
 *
 * @p mono_ms is a local monotonic millisecond clock — the Flipper tick on
 * device, the simulator's own clock under test. It is what the receiver's UTC
 * is judged against, so it must never come from the GPS.
 */
void mrd_detect_epoch(
    MrdDetect* d,
    const MrdFix* fix,
    const MrdSat* sats,
    uint8_t sat_count,
    uint32_t mono_ms);

/* ---- presentation ---- */

const char* mrd_check_name(MrdCheckId id); /**< short, fits a 128px row */
const char* mrd_check_title(MrdCheckId id); /**< full sentence for the detail card */
const char* mrd_check_what(MrdCheckId id); /**< what a real sky does */
const char* mrd_check_benign(MrdCheckId id); /**< the innocent explanation */
const char* mrd_verdict_name(MrdVerdict v);
const char* mrd_state_name(MrdCheckState s);
const char* mrd_jam_reason(uint8_t reason);

/** "s=0.8 dB, expect >3" — the observation, in the check's own units. */
void mrd_check_observed(const MrdDetect* d, MrdCheckId id, char* out, size_t out_len);

/* ---- geodesy, shared with the trail view ---- */

/** Great-circle distance in metres. */
double mrd_geo_distance_m(double lat1, double lon1, double lat2, double lon2);

/** Local east/north offset in metres from a reference point. */
void mrd_geo_offset_m(
    double ref_lat,
    double ref_lon,
    double lat,
    double lon,
    float* east_m,
    float* north_m);
