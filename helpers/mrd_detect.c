#include "mrd_detect.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Declared as variables rather than macros because the firmware builds with
 * -fsingle-precision-constant: an unsuffixed literal is a float, and one of
 * those meeting a double is an error. Initialising a double from it is fine.
 *
 * These carry only float precision, and that is correct rather than a
 * compromise. They are scale factors applied to coordinate *differences* that
 * were computed in double, so their 2e-8 relative error shows up as a 0.02 ppm
 * scale on a distance - 80 micrometres across the four kilometres of a spoof -
 * while the precision that actually matters, the subtraction of two nearly
 * equal coordinates, stays in double throughout.
 */
static const double DEG2RAD = 0.017453292519943295;
static const double GEO_R = 6371008.8; /* IUGG mean Earth radius, metres */

/* =========================================================================
 * Thresholds
 *
 * Every number below is a claim about the physical world, so every number
 * below carries the claim it rests on. They are restated with sources in
 * SPOOFING.md; if one of them is wrong, that is where to argue with it.
 * ========================================================================= */

/* Impossible motion. 120 m/s is 432 km/h — past any road vehicle and most
 * light aircraft. 340 m/s is the speed of sound at sea level. */
#define TH_JUMP_WARN 120.0f
#define TH_JUMP_ALERT 340.0f
#define JUMP_MAX_DT_S 10.0f

/* Doppler speed against position-derived speed. A receiver derives speed over
 * ground from carrier Doppler, not from differencing positions, so the two are
 * computed by independent paths and normally agree to well under 1 m/s. A
 * simulator replaying a canned track frequently forgets to keep them in step.
 * Position noise alone puts ~0.5 m/s of jitter on the derived figure when
 * stationary, so the bar sits well clear of it and needs three epochs. */
#define TH_VELO_WARN 3.0f
#define TH_VELO_ALERT 6.0f
#define VELO_RUN 3

/* Carrier-to-noise spread. An open-sky constellation spans roughly 25 dB-Hz
 * at the horizon to 48 at the zenith, so the population sigma of C/N0 across
 * tracked satellites normally lands between 4 and 10 dB. A transmitter
 * generating every channel from one power amplifier flattens it. */
#define TH_SIGMA_ALERT 1.0f
#define TH_SIGMA_WARN 2.0f
#define MIN_SATS_SIGMA 5

/* Absolute carrier level. A passive patch antenna peaks near 48-50 dB-Hz on
 * the highest satellite; a *mean* above 50 across the whole constellation is
 * not something open sky delivers. To beat the real signal a close spoofer
 * has to arrive hotter, and this is where that shows. */
#define TH_SNR_WARN 47.0f
#define TH_SNR_ALERT 50.0f
#define MIN_SATS_SNR 4

/* Below this the whole constellation is down in the noise, and the shape of
 * the C/N0 distribution stops meaning anything: sigma and the elevation
 * correlation both become measurements of thermal noise. Under a jammer, or
 * indoors, that is exactly what happens - so those two checks stand down
 * rather than reporting noise as evidence. */
#define MIN_MEAN_FOR_SHAPE 25.0f

/* Power against elevation. Higher satellites travel through less atmosphere
 * and suffer less ground multipath, so C/N0 rises with elevation: r is
 * typically +0.4 to +0.8. A spoofer sits at one point on the ground, so every
 * fake satellite shares one real geometry and the relationship disappears.
 * The bar is set below zero, not at it, because urban multipath can flatten a
 * genuine sky. */
#define TH_R_WARN -0.05f
#define TH_R_ALERT -0.35f
#define MIN_SATS_CORR 6
#define MIN_ELEV_SPAN 30

/* Receiver clock against the local clock. The Flipper's tick runs off the
 * 32.768 kHz crystal at a few tens of ppm, so honest drift over an hour is
 * under a tenth of a second; seconds of disagreement are not the crystal. */
#define TH_CLOCK_JUMP_MS 2000
#define TH_DRIFT_WARN_MS 5000
#define TH_DRIFT_ALERT_MS 15000

/* Frozen position. Receiver noise moves a stationary fix by a metre or two
 * every second, so bit-identical coordinates epoch after epoch mean the
 * number is being recited rather than computed. */
#define TH_FROZEN_ALERT 14 /* of MRD_FROZEN_WIN */
#define TH_FROZEN_WARN 10

/* Sky motion. A GPS satellite crosses the sky in a few hours; apparent
 * elevation moves fastest low down at well over 1 deg/min and slowest at
 * culmination. Across six or more satellites, four minutes without a single
 * whole-degree change is not a sky. */
#define SKY_WINDOW_MS 240000u
#define MIN_SATS_SKY 6

/* Dilution of precision. Consumer multi-GNSS receivers reach HDOP ~0.5 with a
 * full sky; below 0.4 is a claim the geometry cannot support. Separately, a
 * DOP that never changes while the satellite count does is a canned field. */
#define TH_HDOP_LOW 0.40f
#define HDOP_SAME_RUN 12

/* Altitude. 200 m in a second is a lift no vehicle performs; a 3D fix pinned
 * at exactly zero for twenty seconds is a simulator that never bothered. */
#define TH_ALT_WARN 200.0f
#define TH_ALT_ALERT 1000.0f
#define ALT_ZERO_RUN 20

/* Capture: lock lost, then regained somewhere else. Judged as implied speed
 * across the outage so that genuinely driving out of a tunnel does not fire. */
#define TH_CAP_WARN 45.0f
#define TH_CAP_ALERT 90.0f
#define CAP_MIN_OUTAGE_MS 2000u

/* Jamming, which is denial rather than deception and is reported separately. */
#define TH_JAM_SNR 20.0f
#define JAM_NOFIX_RUN 3

/*
 * A single antenna cannot prove anything about GPS, so the score never reaches
 * a hundred. This ceiling is the tool declining to claim certainty it does not
 * have, and it is deliberate.
 */
#define MRD_SCORE_CEILING 96

/*
 * Independence families.
 *
 * The three carrier checks all read the same C/N0 numbers, so three of them
 * firing together is one observation restated, not three. Corroboration is
 * therefore counted in families - position, carrier, geometry, time - because
 * those really are measured through different paths and an attacker has to get
 * all of them right separately.
 */
typedef enum {
    FamPosition = 0,
    FamCarrier,
    FamGeometry,
    FamTime,
    FamCount,
} MrdFamily;

/*
 * Weight is how much a check contributes to the weighted score. Floor is the
 * score a single ALERT on that check guarantees on its own, so one unambiguous
 * teleport cannot be averaged away by ten quiet checks sitting at OK.
 *
 * Every floor is below the SPOOF LIKELY band on purpose: no lone check, however
 * clean its evidence, is ever allowed to reach that verdict by itself. Getting
 * there takes agreement between families.
 */
static const struct {
    uint8_t weight;
    uint8_t floor;
    uint8_t family;
} CHECK_W[MrdCheckCount] = {
    [MrdCheckJump] = {18, 55, FamPosition},
    [MrdCheckVelocity] = {14, 45, FamPosition},
    [MrdCheckSnrFlat] = {14, 50, FamCarrier},
    [MrdCheckSnrHot] = {12, 45, FamCarrier},
    [MrdCheckSnrElev] = {10, 40, FamCarrier},
    [MrdCheckClock] = {14, 50, FamTime},
    [MrdCheckFrozen] = {8, 30, FamPosition},
    [MrdCheckSkyStatic] = {12, 45, FamGeometry},
    [MrdCheckDop] = {6, 25, FamGeometry},
    [MrdCheckAltitude] = {6, 25, FamPosition},
    [MrdCheckCapture] = {16, 60, FamPosition},
};

/*
 * What agreement between independent families is worth, indexed by how many
 * are alerting at once. One family on its own buys nothing here and is left to
 * its own floor, which is what keeps a single tell out of SPOOF LIKELY.
 */
static const uint8_t FAMILY_FLOOR[FamCount + 1] = {0, 0, 62, 80, 92};

/* ======================================================== small helpers -- */

/** Multiplier for "greater than" thresholds: higher sensitivity lowers the bar. */
static float k_hi(MrdSensitivity s) {
    return (s == MrdSensLow) ? 1.40f : (s == MrdSensHigh) ? 0.70f : 1.0f;
}

/** Multiplier for "less than" thresholds: higher sensitivity raises the bar. */
static float k_lo(MrdSensitivity s) {
    return (s == MrdSensLow) ? 0.70f : (s == MrdSensHigh) ? 1.40f : 1.0f;
}

/** One decimal place without %f, which newlib-nano leaves out of printf. */
static void f1(char* b, size_t n, float v) {
    bool neg = v < 0.0f;
    if(neg) v = -v;
    if(v > 99999.0f) v = 99999.0f;
    uint32_t w = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)w) * 10.0f + 0.5f);
    if(f >= 10) {
        f = 0;
        w++;
    }
    snprintf(b, n, "%s%lu.%lu", neg ? "-" : "", (unsigned long)w, (unsigned long)f);
}

/** Two decimals, for correlation coefficients. */
static void f2(char* b, size_t n, float v) {
    bool neg = v < 0.0f;
    if(neg) v = -v;
    if(v > 999.0f) v = 999.0f;
    uint32_t w = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)w) * 100.0f + 0.5f);
    if(f >= 100) {
        f = 0;
        w++;
    }
    snprintf(b, n, "%s%lu.%02lu", neg ? "-" : "", (unsigned long)w, (unsigned long)f);
}

/* ============================================================== trail -- */

void mrd_trail_push(MrdTrail* t, double lat, double lon) {
    t->pt[t->head].lat = lat;
    t->pt[t->head].lon = lon;
    t->head = (uint8_t)((t->head + 1) % MRD_TRAIL_LEN);
    if(t->count < MRD_TRAIL_LEN) t->count++;
}

const MrdTrailPoint* mrd_trail_at(const MrdTrail* t, uint8_t index) {
    if(index >= t->count) return NULL;
    /* head points at the next slot to write, so once the ring has wrapped the
     * oldest sample is the one sitting there. */
    uint8_t base = (t->count == MRD_TRAIL_LEN) ? t->head : 0;
    return &t->pt[(base + index) % MRD_TRAIL_LEN];
}

/* ============================================================ geodesy -- */

double mrd_geo_distance_m(double lat1, double lon1, double lat2, double lon2) {
    double p1 = lat1 * DEG2RAD;
    double p2 = lat2 * DEG2RAD;
    double dp = p2 - p1;
    double dl = (lon2 - lon1) * DEG2RAD;

    double sdp = sin(dp * MRD_D(0.5));
    double sdl = sin(dl * MRD_D(0.5));
    double a = sdp * sdp + cos(p1) * cos(p2) * sdl * sdl;
    if(a < MRD_D(0)) a = MRD_D(0);
    if(a > MRD_D(1)) a = MRD_D(1);

    return MRD_D(2) * GEO_R * atan2(sqrt(a), sqrt(MRD_D(1) - a));
}

void mrd_geo_offset_m(
    double ref_lat,
    double ref_lon,
    double lat,
    double lon,
    float* east_m,
    float* north_m) {
    /* Degrees to metres on the same sphere mrd_geo_distance_m uses, so a short
     * hop measured either way comes out the same. */
    const double MPD = GEO_R * DEG2RAD;
    *north_m = (float)((lat - ref_lat) * MPD);
    *east_m = (float)((lon - ref_lon) * MPD * cos(ref_lat * DEG2RAD));
}

/* ========================================================== lifecycle -- */

void mrd_detect_init(MrdDetect* d, MrdSensitivity sens, uint16_t hold_seconds) {
    memset(d, 0, sizeof(*d));
    d->sens = sens;
    d->hold_epochs = hold_seconds ? hold_seconds : 60;
    d->verdict = MrdVerdictNoSignal;
    d->prev_utc_ms = MRD_UTC_UNKNOWN;
}

void mrd_detect_reset(MrdDetect* d) {
    MrdSensitivity sens = d->sens;
    uint32_t hold = d->hold_epochs;
    memset(d, 0, sizeof(*d));
    d->sens = sens;
    d->hold_epochs = hold;
    d->verdict = MrdVerdictNoSignal;
    d->prev_utc_ms = MRD_UTC_UNKNOWN;
}

void mrd_detect_set_sensitivity(MrdDetect* d, MrdSensitivity sens) {
    d->sens = sens;
}

/* ============================================================ flagging -- */

static void flag_hold(MrdDetect* d, MrdCheckId id, uint8_t state, float value, uint32_t hold) {
    MrdCheck* c = &d->checks[id];
    c->armed = true;
    c->value = value;
    if(c->state == MrdStateIdle) c->state = MrdStateOk;

    if(state <= MrdStateOk) return;

    /* Never downgrade inside the hold window: a teleport is one epoch long,
     * and a screen that clears it before the user looks up is useless. The
     * decay pass at the top of the next epoch is what lets it fall back. */
    if(state >= c->state) c->state = state;
    c->hold_until = d->epoch + hold;
    if(c->hits < UINT16_MAX) c->hits++;
    c->last_epoch = d->epoch;
}

static void flag(MrdDetect* d, MrdCheckId id, uint8_t state, float value) {
    flag_hold(d, id, state, value, d->hold_epochs);
}

/* ======================================================== sky measures -- */

typedef struct {
    uint8_t tracked;
    float mean;
    float sigma;
    float r; /* elevation against C/N0 */
    bool r_valid;
} SkyStats;

static void sky_stats(const MrdSat* sats, uint8_t n, SkyStats* out) {
    memset(out, 0, sizeof(*out));

    float sum = 0.0f;
    uint8_t k = 0;
    for(uint8_t i = 0; i < n; i++) {
        if(sats[i].snr == 0) continue; /* in view but not tracked */
        sum += (float)sats[i].snr;
        k++;
    }
    out->tracked = k;
    if(k == 0) return;

    out->mean = sum / (float)k;

    float var = 0.0f;
    for(uint8_t i = 0; i < n; i++) {
        if(sats[i].snr == 0) continue;
        float dv = (float)sats[i].snr - out->mean;
        var += dv * dv;
    }
    out->sigma = sqrtf(var / (float)k);

    /* Pearson r over the tracked set, but only where the sky is actually
     * spread out: a handful of satellites bunched at one elevation says
     * nothing about how power varies with elevation. */
    uint8_t lo = 90, hi = 0, m = 0;
    float ex = 0.0f, ey = 0.0f;
    for(uint8_t i = 0; i < n; i++) {
        if(sats[i].snr == 0 || sats[i].elev == 0) continue;
        if(sats[i].elev < lo) lo = sats[i].elev;
        if(sats[i].elev > hi) hi = sats[i].elev;
        ex += (float)sats[i].elev;
        ey += (float)sats[i].snr;
        m++;
    }
    if(m < MIN_SATS_CORR || (hi - lo) < MIN_ELEV_SPAN) return;

    ex /= (float)m;
    ey /= (float)m;

    float sxy = 0.0f, sxx = 0.0f, syy = 0.0f;
    for(uint8_t i = 0; i < n; i++) {
        if(sats[i].snr == 0 || sats[i].elev == 0) continue;
        float dx = (float)sats[i].elev - ex;
        float dy = (float)sats[i].snr - ey;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    if(sxx <= 0.0f || syy <= 0.0f) return;

    out->r = sxy / sqrtf(sxx * syy);
    out->r_valid = true;
}

/* ============================================================== checks -- */

static void check_motion(MrdDetect* d, const MrdFix* fix, uint32_t mono_ms) {
    if(!fix->valid || !d->prev_valid) return;

    float dt = (float)(mono_ms - d->prev_mono_ms) / 1000.0f;
    if(dt <= 0.05f || dt > JUMP_MAX_DT_S) return;

    double dist = mrd_geo_distance_m(d->prev_lat, d->prev_lon, fix->lat, fix->lon);
    float implied = (float)dist / dt;
    d->speed_derived = implied;

    float kh = k_hi(d->sens);
    uint8_t st = MrdStateOk;
    if(implied > TH_JUMP_ALERT * kh) {
        st = MrdStateAlert;
    } else if(implied > TH_JUMP_WARN * kh) {
        st = MrdStateWarn;
    }
    flag(d, MrdCheckJump, st, implied);

    /* Doppler speed against the position-derived figure. Only meaningful at a
     * normal update rate; a stale previous epoch makes the derived number
     * meaningless. */
    if(fix->has_speed && dt <= 3.0f) {
        float diff = fabsf(fix->speed_mps - implied);
        uint8_t vs = MrdStateOk;

        if(diff > TH_VELO_ALERT * kh) {
            if(d->velo_run < 255) d->velo_run++;
            if(d->velo_run >= VELO_RUN) vs = MrdStateAlert;
        } else if(diff > TH_VELO_WARN * kh) {
            if(d->velo_run < 255) d->velo_run++;
            if(d->velo_run >= VELO_RUN) vs = MrdStateWarn;
        } else {
            d->velo_run = 0;
        }
        flag(d, MrdCheckVelocity, vs, diff);
    }
}

static void check_frozen(MrdDetect* d, const MrdFix* fix) {
    if(!fix->valid || !d->prev_valid) return;

    /* Exact equality is the point. The parser turns identical text into
     * identical bits, so this asks "did the receiver emit the same digits
     * again", not "did it move less than some epsilon". */
    bool same = (fix->lat == d->prev_lat) && (fix->lon == d->prev_lon);

    d->frozen_bits = (d->frozen_bits << 1) | (same ? 1u : 0u);
    if(d->frozen_filled < MRD_FROZEN_WIN) d->frozen_filled++;
    if(d->frozen_filled < MRD_FROZEN_WIN) return;

    uint8_t n = 0;
    for(uint8_t i = 0; i < MRD_FROZEN_WIN; i++) {
        if(d->frozen_bits & (1u << i)) n++;
    }

    /* Four satellites is the minimum for a 3D solution; below that a receiver
     * may be repeating its last good fix for entirely honest reasons. */
    uint8_t st = MrdStateOk;
    if(fix->sats_used >= 4) {
        float kl = k_lo(d->sens);
        if((float)n >= (float)TH_FROZEN_ALERT / kl) {
            st = MrdStateAlert;
        } else if((float)n >= (float)TH_FROZEN_WARN / kl) {
            st = MrdStateWarn;
        }
    }
    flag(d, MrdCheckFrozen, st, (float)n);
}

static void check_clock(MrdDetect* d, const MrdFix* fix, uint32_t mono_ms) {
    if(fix->utc_ms == MRD_UTC_UNKNOWN || d->prev_utc_ms == MRD_UTC_UNKNOWN) return;

    int64_t d_utc = (int64_t)fix->utc_ms - (int64_t)d->prev_utc_ms;
    /* Midnight: UTC ms-of-day wraps to zero rather than going backwards. */
    if(d_utc < -80000000LL) d_utc += 86400000LL;

    int64_t d_mono = (int64_t)(mono_ms - d->prev_mono_ms);
    if(d_mono <= 0) return;

    int64_t err = d_utc - d_mono;
    int64_t aerr = err < 0 ? -err : err;

    /* Accumulated in whole milliseconds. Summing thousands of small floats
     * would quietly lose the very drift this check exists to notice. */
    d->drift_accum_ms += err;
    d->clock_drift_s = (float)d->drift_accum_ms / 1000.0f;

    float kh = k_hi(d->sens);
    uint8_t st = MrdStateOk;

    if(d_utc < 0) {
        /* Receiver time ran backwards. Nothing benign does this. */
        st = MrdStateAlert;
    } else if((float)aerr > (float)TH_CLOCK_JUMP_MS * kh) {
        st = MrdStateAlert;
    } else {
        int64_t ad = d->drift_accum_ms < 0 ? -d->drift_accum_ms : d->drift_accum_ms;
        if((float)ad > (float)TH_DRIFT_ALERT_MS * kh) {
            st = MrdStateAlert;
        } else if((float)ad > (float)TH_DRIFT_WARN_MS * kh) {
            st = MrdStateWarn;
        }
    }
    flag(d, MrdCheckClock, st, d->clock_drift_s);
}

static void check_altitude(MrdDetect* d, const MrdFix* fix, uint32_t mono_ms) {
    if(!fix->valid || !fix->has_alt) return;

    if(fix->fix_type == 3 && fix->alt_m == 0.0f) {
        if(d->alt_zero_run < 255) d->alt_zero_run++;
    } else {
        d->alt_zero_run = 0;
    }

    uint8_t st = MrdStateOk;
    float shown = fix->alt_m;

    if(d->prev_valid) {
        float dt = (float)(mono_ms - d->prev_mono_ms) / 1000.0f;
        if(dt > 0.05f && dt <= 5.0f) {
            float step = fabsf(fix->alt_m - d->prev_alt);
            float kh = k_hi(d->sens);
            if(step > TH_ALT_ALERT * kh) {
                st = MrdStateAlert;
                shown = step;
            } else if(step > TH_ALT_WARN * kh) {
                st = MrdStateWarn;
                shown = step;
            }
        }
    }

    if(st == MrdStateOk && d->alt_zero_run >= ALT_ZERO_RUN) {
        st = MrdStateWarn;
        shown = 0.0f;
    }
    flag(d, MrdCheckAltitude, st, shown);
}

static void check_dop(MrdDetect* d, const MrdFix* fix) {
    if(!fix->valid || fix->hdop <= 0.0f) return;

    /* A DOP that never moves while the satellite count does is a constant
     * being recited. Tracked to a hundredth, which is the resolution every
     * receiver reports it at. */
    bool same = fabsf(fix->hdop - d->last_hdop) < 0.005f;
    if(same) {
        if(d->hdop_same_run < 255) d->hdop_same_run++;
    } else {
        d->hdop_same_run = 0;
    }
    d->last_hdop = fix->hdop;

    float kl = k_lo(d->sens);
    uint8_t st = MrdStateOk;

    if(d->hdop_same_run >= HDOP_SAME_RUN && fix->sats_used >= 4) {
        st = MrdStateAlert;
    } else if(fix->hdop < TH_HDOP_LOW * kl) {
        st = MrdStateWarn;
    }
    flag(d, MrdCheckDop, st, fix->hdop);
}

static void check_snr(MrdDetect* d, const SkyStats* s) {
    float kl = k_lo(d->sens);
    bool shape_meaningful = s->mean >= MIN_MEAN_FOR_SHAPE;

    if(s->tracked >= MIN_SATS_SIGMA && shape_meaningful) {
        uint8_t st = MrdStateOk;
        if(s->sigma < TH_SIGMA_ALERT * kl) {
            st = MrdStateAlert;
        } else if(s->sigma < TH_SIGMA_WARN * kl) {
            st = MrdStateWarn;
        }
        flag(d, MrdCheckSnrFlat, st, s->sigma);
    }

    if(s->tracked >= MIN_SATS_SNR) {
        /*
         * Absolute power, so sensitivity shifts it by a fixed decibel margin
         * rather than scaling it. 50 dB-Hz is 50 dB-Hz however paranoid the
         * user is feeling; what the setting expresses is how much allowance to
         * make for a high-gain active antenna, and that is an offset.
         */
        float bias = (d->sens == MrdSensLow) ? 1.5f : (d->sens == MrdSensHigh) ? -1.5f : 0.0f;
        uint8_t st = MrdStateOk;
        if(s->mean > TH_SNR_ALERT + bias) {
            st = MrdStateAlert;
        } else if(s->mean > TH_SNR_WARN + bias) {
            st = MrdStateWarn;
        }
        flag(d, MrdCheckSnrHot, st, s->mean);
    }

    if(s->r_valid && shape_meaningful) {
        uint8_t st = MrdStateOk;
        if(s->r < TH_R_ALERT * kl) {
            st = MrdStateAlert;
        } else if(s->r < TH_R_WARN) {
            st = MrdStateWarn;
        }
        flag(d, MrdCheckSnrElev, st, s->r);
    }
}

static void check_sky_motion(MrdDetect* d, const MrdSat* sats, uint8_t n, uint32_t mono_ms) {
    if(d->sky_snap_count == 0) {
        goto snapshot;
    }
    if(mono_ms - d->sky_snap_mono_ms < SKY_WINDOW_MS) return;

    uint8_t compared = 0, moved = 0;
    for(uint8_t i = 0; i < d->sky_snap_count; i++) {
        for(uint8_t j = 0; j < n; j++) {
            if(sats[j].sys != d->sky_snap[i].sys || sats[j].prn != d->sky_snap[i].prn) continue;
            if(sats[j].elev == 0) break; /* dropped below the mask; says nothing */
            compared++;
            int diff = (int)sats[j].elev - (int)d->sky_snap[i].elev;
            if(diff < 0) diff = -diff;
            if(diff >= 1) moved++;
            break;
        }
    }

    if(compared >= MIN_SATS_SKY) {
        uint8_t st = MrdStateOk;
        if(moved == 0) {
            st = MrdStateAlert;
        } else if((float)moved < (float)compared * 0.25f) {
            st = MrdStateWarn;
        }
        /* This check only produces a verdict once per window, so it has to
         * latch for a whole window plus slack rather than the usual hold. */
        flag_hold(d, MrdCheckSkyStatic, st, (float)moved, (SKY_WINDOW_MS / 1000u) + 30u);
    }

snapshot:
    d->sky_snap_count = 0;
    for(uint8_t i = 0; i < n && d->sky_snap_count < MRD_SKY_SNAP; i++) {
        if(sats[i].snr == 0 || sats[i].elev == 0) continue;
        d->sky_snap[d->sky_snap_count].sys = sats[i].sys;
        d->sky_snap[d->sky_snap_count].prn = sats[i].prn;
        d->sky_snap[d->sky_snap_count].elev = sats[i].elev;
        d->sky_snap_count++;
    }
    d->sky_snap_mono_ms = mono_ms;
}

static void check_capture(MrdDetect* d, const MrdFix* fix, uint32_t mono_ms) {
    if(!fix->valid) return;

    if(d->have_last_good && !d->prev_valid) {
        /* The fix has just come back after an outage. */
        uint32_t gap_ms = mono_ms - d->last_good_mono_ms;
        if(gap_ms >= CAP_MIN_OUTAGE_MS) {
            double dist =
                mrd_geo_distance_m(d->last_good_lat, d->last_good_lon, fix->lat, fix->lon);
            float implied = (float)dist / ((float)gap_ms / 1000.0f);

            float kh = k_hi(d->sens);
            uint8_t st = MrdStateOk;
            if(implied > TH_CAP_ALERT * kh) {
                st = MrdStateAlert;
            } else if(implied > TH_CAP_WARN * kh) {
                st = MrdStateWarn;
            }
            flag(d, MrdCheckCapture, st, implied);
        }
    }

    d->have_last_good = true;
    d->last_good_lat = fix->lat;
    d->last_good_lon = fix->lon;
    d->last_good_mono_ms = mono_ms;
}

static void check_jamming(MrdDetect* d, const MrdFix* fix, const SkyStats* s) {
    uint8_t jam = MrdJamNone;

    if(fix->sats_view >= 4 && fix->sats_used == 0) {
        if(d->nofix_run < 255) d->nofix_run++;
        if(d->nofix_run >= JAM_NOFIX_RUN) jam = MrdJamNoSolution;
    } else {
        d->nofix_run = 0;
    }

    if(jam == MrdJamNone && s->tracked >= 4 && s->mean < TH_JAM_SNR) jam = MrdJamFloorUp;
    if(jam == MrdJamNone && !fix->valid && fix->sats_view >= 4) jam = MrdJamLostFix;

    d->jamming = (jam != MrdJamNone);
    d->jam_reason = jam;
}

/* =============================================================== score -- */

static void score_and_verdict(MrdDetect* d) {
    uint32_t got = 0, total = 0;
    uint8_t armed = 0, alerts = 0, warns = 0, floor = 0;
    bool fam_hit[FamCount] = {false, false, false, false};

    for(uint8_t i = 0; i < MrdCheckCount; i++) {
        const MrdCheck* c = &d->checks[i];
        if(!c->armed) continue;

        armed++;
        total += CHECK_W[i].weight;

        if(c->state == MrdStateAlert) {
            alerts++;
            got += CHECK_W[i].weight * 100u;
            if(CHECK_W[i].floor > floor) floor = CHECK_W[i].floor;
            fam_hit[CHECK_W[i].family] = true;
        } else if(c->state == MrdStateWarn) {
            warns++;
            got += CHECK_W[i].weight * 45u;
        }
    }

    uint8_t families = 0;
    for(uint8_t f = 0; f < FamCount; f++) {
        if(fam_hit[f]) families++;
    }

    d->armed = armed;
    d->alerts = alerts;
    d->warns = warns;
    d->families = families;

    /*
     * The weighted average says how much of what was measured looks wrong.
     * The floors say that some single observations are damning on their own,
     * and must not be diluted by the checks that happen to be quiet. The
     * family term says that agreement between independent measurement paths
     * is worth more than either - which is how evidence actually works.
     */
    uint32_t score = total ? (got / total) : 0;
    if(floor > score) score = floor;
    if(FAMILY_FLOOR[families] > score) score = FAMILY_FLOOR[families];
    if(score > MRD_SCORE_CEILING) score = MRD_SCORE_CEILING;
    d->score = (uint8_t)score;

    if(d->epochs_valid == 0 && armed == 0) {
        d->verdict = MrdVerdictNoSignal;
    } else if(armed < 4 && score < 50) {
        /* Not enough independent evidence to say anything — but a hard alert
         * during warm-up is still an alert, and is not swallowed. */
        d->verdict = MrdVerdictWarmup;
    } else if(score >= 65) {
        d->verdict = MrdVerdictLikely;
    } else if(score >= 40) {
        d->verdict = MrdVerdictSuspect;
    } else if(score >= 20) {
        d->verdict = MrdVerdictAnomalous;
    } else {
        d->verdict = MrdVerdictNominal;
    }
}

/* =============================================================== epoch -- */

void mrd_detect_epoch(
    MrdDetect* d,
    const MrdFix* fix,
    const MrdSat* sats,
    uint8_t sat_count,
    uint32_t mono_ms) {
    d->epoch++;

    /* Latched flags fall back once their hold expires. Done first, so a flag
     * raised this epoch gets a full hold window from here. */
    for(uint8_t i = 0; i < MrdCheckCount; i++) {
        MrdCheck* c = &d->checks[i];
        if(c->state > MrdStateOk && d->epoch >= c->hold_until) c->state = MrdStateOk;
    }

    SkyStats s;
    sky_stats(sats, sat_count, &s);
    d->sats_tracked = s.tracked;
    d->snr_mean = s.mean;
    d->snr_sigma = s.sigma;
    d->snr_elev_r = s.r_valid ? s.r : 0.0f;

    check_snr(d, &s);
    check_sky_motion(d, sats, sat_count, mono_ms);
    check_jamming(d, fix, &s);

    if(fix->valid) {
        d->epochs_valid++;

        check_motion(d, fix, mono_ms);
        check_frozen(d, fix);
        check_clock(d, fix, mono_ms);
        check_altitude(d, fix, mono_ms);
        check_dop(d, fix);
        check_capture(d, fix, mono_ms);
    } else {
        /* An outage resets the runs that only mean anything while locked. */
        d->velo_run = 0;
        d->frozen_bits = 0;
        d->frozen_filled = 0;
    }

    score_and_verdict(d);

    d->prev_valid = fix->valid;
    d->prev_lat = fix->lat;
    d->prev_lon = fix->lon;
    d->prev_alt = fix->alt_m;
    d->prev_utc_ms = fix->utc_ms;
    d->prev_mono_ms = mono_ms;
}

/* ======================================================== presentation -- */

const char* mrd_check_name(MrdCheckId id) {
    switch(id) {
    case MrdCheckJump:
        return "Impossible motion";
    case MrdCheckVelocity:
        return "Speed mismatch";
    case MrdCheckSnrFlat:
        return "Flat carrier power";
    case MrdCheckSnrHot:
        return "Carrier too strong";
    case MrdCheckSnrElev:
        return "Power vs elevation";
    case MrdCheckClock:
        return "Clock inconsistent";
    case MrdCheckFrozen:
        return "Position frozen";
    case MrdCheckSkyStatic:
        return "Sky not moving";
    case MrdCheckDop:
        return "Accuracy implausible";
    case MrdCheckAltitude:
        return "Altitude anomaly";
    case MrdCheckCapture:
        return "Lock captured";
    default:
        return "?";
    }
}

const char* mrd_check_title(MrdCheckId id) {
    switch(id) {
    case MrdCheckJump:
        return "Position moved faster than physics allows";
    case MrdCheckVelocity:
        return "Doppler speed disagrees with the track";
    case MrdCheckSnrFlat:
        return "Every satellite arrives at the same power";
    case MrdCheckSnrHot:
        return "Signals are louder than open sky delivers";
    case MrdCheckSnrElev:
        return "Power no longer rises with elevation";
    case MrdCheckClock:
        return "Receiver time is not tracking real time";
    case MrdCheckFrozen:
        return "The fix repeats to the last digit";
    case MrdCheckSkyStatic:
        return "The constellation has stopped moving";
    case MrdCheckDop:
        return "Claimed accuracy is not supportable";
    case MrdCheckAltitude:
        return "Altitude jumped or is pinned";
    case MrdCheckCapture:
        return "Lock returned somewhere else entirely";
    default:
        return "?";
    }
}

const char* mrd_check_what(MrdCheckId id) {
    switch(id) {
    case MrdCheckJump:
        return "Consecutive fixes imply a ground speed no vehicle reaches. A "
               "spoofer taking over an already-locked receiver drags it to the "
               "false position in one step.";
    case MrdCheckVelocity:
        return "Speed over ground comes from carrier Doppler, not from "
               "differencing positions. The two paths are independent, so on a "
               "real receiver they agree. Replayed tracks often forget this.";
    case MrdCheckSnrFlat:
        return "Real satellites sit at different elevations and arrive at "
               "different powers, spread over 4-10 dB. One transmitter feeding "
               "every fake channel flattens that spread to nothing.";
    case MrdCheckSnrHot:
        return "To beat the real signal, a spoofer has to arrive louder than "
               "it. A whole constellation averaging above 50 dB-Hz is not "
               "something a patch antenna sees from orbit.";
    case MrdCheckSnrElev:
        return "High satellites cut through less atmosphere and less ground "
               "clutter, so power climbs with elevation. A spoofer transmits "
               "from one spot, so all its satellites share one geometry.";
    case MrdCheckClock:
        return "GPS time advances at one second per second. Meridian compares "
               "it against the Flipper's own clock, which the attacker does "
               "not control. Time-shifting attacks show up here first.";
    case MrdCheckFrozen:
        return "Even bolted to a wall, a receiver's fix wanders a metre or two "
               "each second from noise and multipath. Coordinates repeating "
               "bit-for-bit mean the number is recited, not solved.";
    case MrdCheckSkyStatic:
        return "Satellites rise and set. Over four minutes at least one of six "
               "should change elevation by a whole degree. A canned sky holds "
               "still.";
    case MrdCheckDop:
        return "Dilution of precision follows from satellite geometry, so it "
               "changes as the sky does. A value below 0.4, or one that never "
               "moves while the satellite count does, was not computed.";
    case MrdCheckAltitude:
        return "Height is the weakest axis of a GPS solution and the one "
               "simulators most often neglect - pinned at zero, or stepping by "
               "hundreds of metres between seconds.";
    case MrdCheckCapture:
        return "The classic takeover: hold the receiver down until it loses "
               "lock, then hand it a stronger fake. The gap and the distance "
               "across it imply a speed nothing on the ground reaches.";
    default:
        return "";
    }
}

const char* mrd_check_benign(MrdCheckId id) {
    switch(id) {
    case MrdCheckJump:
        return "A cold receiver's first fixes can land far from the second. "
               "Meridian only counts jumps between two locked epochs.";
    case MrdCheckVelocity:
        return "Stop-start driving and heavy multipath in a city can pull the "
               "two figures apart for a second or two, which is why this needs "
               "three epochs running.";
    case MrdCheckSnrFlat:
        return "A clear view of only a few satellites at similar elevations "
               "narrows the spread honestly. Under five tracked, this does not "
               "run at all.";
    case MrdCheckSnrHot:
        return "An active antenna with a high-gain LNA reports higher C/N0 "
               "than a passive patch. If yours does, expect this one to sit "
               "warm and read the others instead.";
    case MrdCheckSnrElev:
        return "Dense urban multipath, a tilted antenna, or a metal roof over "
               "half the sky can flatten or invert the relationship without "
               "any attacker involved.";
    case MrdCheckClock:
        return "A receiver that has just acquired may step its clock once as "
               "it solves for time. One step at start-up is normal; repeated "
               "or growing disagreement is not.";
    case MrdCheckFrozen:
        return "Many receivers ship with static-hold or position-pinning "
               "enabled, which freezes output deliberately when it decides you "
               "are stationary. Check your module's configuration first.";
    case MrdCheckSkyStatic:
        return "If the receiver only refreshes GSV every few minutes, or the "
               "sky is mostly blocked, too few satellites survive the "
               "comparison and this stays quiet.";
    case MrdCheckDop:
        return "Multi-GNSS receivers with a full sky legitimately reach 0.5. "
               "A repeated value matters more than a low one.";
    case MrdCheckAltitude:
        return "2D fixes report a fixed or absent altitude by design. This "
               "only reads pinned zeroes when the receiver claims a 3D fix.";
    case MrdCheckCapture:
        return "Tunnels, car parks and long indoor gaps all end with a fix "
               "reappearing somewhere new. The implied speed across the gap is "
               "what separates driving from teleporting.";
    default:
        return "";
    }
}

const char* mrd_verdict_name(MrdVerdict v) {
    switch(v) {
    case MrdVerdictNoSignal:
        return "NO SIGNAL";
    case MrdVerdictWarmup:
        return "WARMING UP";
    case MrdVerdictNominal:
        return "NOMINAL";
    case MrdVerdictAnomalous:
        return "ANOMALOUS";
    case MrdVerdictSuspect:
        return "SUSPECT";
    case MrdVerdictLikely:
        return "SPOOF LIKELY";
    default:
        return "?";
    }
}

const char* mrd_state_name(MrdCheckState s) {
    switch(s) {
    case MrdStateOk:
        return "OK";
    case MrdStateWarn:
        return "WARN";
    case MrdStateAlert:
        return "ALERT";
    default:
        return "--";
    }
}

const char* mrd_jam_reason(uint8_t reason) {
    switch(reason) {
    case MrdJamNoSolution:
        return "Satellites in view, none usable";
    case MrdJamFloorUp:
        return "Carrier powers collapsed";
    case MrdJamLostFix:
        return "Fix lost with the sky still visible";
    default:
        return "";
    }
}

void mrd_check_observed(const MrdDetect* d, MrdCheckId id, char* out, size_t out_len) {
    const MrdCheck* c = &d->checks[id];
    char v[16];

    if(!c->armed) {
        snprintf(out, out_len, "not enough data yet");
        return;
    }

    switch(id) {
    case MrdCheckJump:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "%s m/s implied, limit %d", v, (int)TH_JUMP_WARN);
        break;
    case MrdCheckVelocity:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "%s m/s apart, expect <1", v);
        break;
    case MrdCheckSnrFlat:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "spread %s dB, expect 4-10", v);
        break;
    case MrdCheckSnrHot:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "mean %s dB-Hz, expect 35-45", v);
        break;
    case MrdCheckSnrElev:
        f2(v, sizeof(v), c->value);
        snprintf(out, out_len, "r = %s, expect +0.4 to +0.8", v);
        break;
    case MrdCheckClock:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "%s s off local clock", v);
        break;
    case MrdCheckFrozen:
        snprintf(
            out, out_len, "%d of %d fixes identical", (int)c->value, (int)MRD_FROZEN_WIN);
        break;
    case MrdCheckSkyStatic:
        snprintf(out, out_len, "%d satellites moved in 4 min", (int)c->value);
        break;
    case MrdCheckDop:
        f2(v, sizeof(v), c->value);
        /* The run counter saturates, so say so rather than quietly understate
         * how long a canned value has been sitting there. */
        snprintf(
            out,
            out_len,
            "HDOP %s, held %d%s epochs",
            v,
            (int)d->hdop_same_run,
            d->hdop_same_run >= 255 ? "+" : "");
        break;
    case MrdCheckAltitude:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "%s m step", v);
        break;
    case MrdCheckCapture:
        f1(v, sizeof(v), c->value);
        snprintf(out, out_len, "%s m/s across the outage", v);
        break;
    default:
        snprintf(out, out_len, "-");
        break;
    }
}
