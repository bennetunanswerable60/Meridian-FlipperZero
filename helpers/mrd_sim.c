#include "mrd_sim.h"
#include "mrd_nmea.h"

#include <stdio.h>
#include <string.h>

/*
 * Home is the Royal Observatory at Greenwich, on the prime meridian itself.
 * A neutral, public, and thematically unavoidable place to stand.
 */
#define SIM_LAT MRD_D(51.4779)
#define SIM_LON MRD_D(-0.0015)
#define SIM_ALT 45.0f

/* Where the spoof puts you: four kilometres off, which is the realistic case.
 * A competent spoofer moves you somewhere plausible, not to Null Island. */
#define SPOOF_LAT (SIM_LAT + MRD_D(0.0200))
#define SPOOF_LON (SIM_LON - MRD_D(0.0500))

/*
 * The sky. Eleven satellites with their own azimuths and their own elevation
 * rates, in tenths of a degree per minute - fast low down, near zero at
 * culmination, which is how the geometry actually behaves.
 */
static const struct {
    uint8_t prn;
    uint16_t az;
    uint8_t elev0;
    int8_t rate10;
} SIM_SKY[] = {
    {2, 312, 68, -4},
    {5, 44, 41, 9},
    {6, 128, 12, 11},
    {9, 201, 55, -7},
    {12, 75, 23, 12},
    {17, 249, 34, 8},
    {19, 156, 77, -3},
    {23, 18, 8, 13},
    {24, 288, 19, -10},
    {28, 95, 47, 6},
    {31, 340, 29, -9},
};
#define SIM_SKY_N ((uint8_t)(sizeof(SIM_SKY) / sizeof(SIM_SKY[0])))

/* --------------------------------------------------------------- rng --- */

static uint32_t xs32(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x1234567u;
    return *s;
}

/** Uniform integer in [-span, +span]. */
static int rnd_span(uint32_t* s, int span) {
    return (int)(xs32(s) % (uint32_t)(2 * span + 1)) - span;
}

/* ------------------------------------------------------------ format --- */

static void put_checksum(char* line, size_t len) {
    uint8_t cs = mrd_nmea_checksum(line + 1, strlen(line + 1));
    size_t n = strlen(line);
    static const char hex[] = "0123456789ABCDEF";
    if(n + 4 > len) return;
    line[n] = '*';
    line[n + 1] = hex[(cs >> 4) & 0xF];
    line[n + 2] = hex[cs & 0xF];
    line[n + 3] = '\0';
}

/** Degrees to ddmm.mmmm / dddmm.mmmm, without %f. */
static void fmt_deg(char* out, size_t n, double deg, bool is_lon, char* hemi) {
    bool neg = deg < MRD_D(0);
    if(neg) deg = -deg;
    *hemi = is_lon ? (neg ? 'W' : 'E') : (neg ? 'S' : 'N');

    /* Clamped so the field widths below are provably sufficient rather than
     * merely usual - the compiler checks this, and it is right to. */
    double limit = is_lon ? MRD_D(180) : MRD_D(90);
    if(deg > limit) deg = limit;

    uint32_t d = (uint32_t)deg;
    double m = (deg - (double)d) * MRD_D(60);
    uint32_t mi = (uint32_t)m;
    uint32_t mf = (uint32_t)((m - (double)mi) * MRD_D(10000) + MRD_D(0.5));
    if(mf >= 10000) {
        mf -= 10000;
        mi++;
    }
    if(mi >= 60) {
        mi -= 60;
        d++;
    }

    /* Restated as hard bounds so the field widths below are provable rather
     * than merely true: without these the compiler has to assume ten digits
     * per %u and rejects the call. */
    if(d > 180) d = 180;
    if(mi > 59) mi = 59;
    if(mf > 9999) mf = 9999;

    if(is_lon) {
        snprintf(out, n, "%03u%02u.%04u", (unsigned)d, (unsigned)mi, (unsigned)mf);
    } else {
        snprintf(out, n, "%02u%02u.%04u", (unsigned)d, (unsigned)mi, (unsigned)mf);
    }
}

/** Milliseconds since midnight to hhmmss.ss. */
static void fmt_time(char* out, size_t n, uint32_t ms) {
    uint32_t total = ms / 1000u;
    uint32_t cs = (ms % 1000u) / 10u;
    snprintf(
        out,
        n,
        "%02u%02u%02u.%02u",
        (unsigned)((total / 3600u) % 24u),
        (unsigned)((total / 60u) % 60u),
        (unsigned)(total % 60u),
        (unsigned)cs);
}

/**
 * A float as "12.34", again without %f.
 *
 * Written out per decimal count rather than with a computed scale, because a
 * runtime divisor leaves the compiler unable to bound the remainder and the
 * build - correctly - will not take a format width it cannot verify.
 */
static void fmt_num(char* out, size_t n, float v, uint8_t dp) {
    const char* sign = (v < 0.0f) ? "-" : "";
    if(v < 0.0f) v = -v;
    if(v > 99999.0f) v = 99999.0f;

    if(dp == 0) {
        uint32_t t = (uint32_t)(v + 0.5f);
        if(t > 99999) t = 99999;
        snprintf(out, n, "%s%u", sign, (unsigned)t);
    } else if(dp == 1) {
        uint32_t t = (uint32_t)(v * 10.0f + 0.5f);
        uint32_t w = t / 10u, f = t % 10u;
        if(w > 99999) w = 99999;
        snprintf(out, n, "%s%u.%u", sign, (unsigned)w, (unsigned)f);
    } else {
        uint32_t t = (uint32_t)(v * 100.0f + 0.5f);
        uint32_t w = t / 100u, f = t % 100u;
        if(w > 99999) w = 99999;
        snprintf(out, n, "%s%u.%02u", sign, (unsigned)w, (unsigned)f);
    }
}

/* --------------------------------------------------------------- sky --- */

typedef struct {
    uint8_t prn;
    uint8_t elev;
    uint16_t az;
    uint8_t snr;
} SimSat;

/** True for the scenarios that fake a sky, as opposed to attacking the clock
 * (which leaves the sky alone) or simply denying it (which does not fake
 * anything at all). Only these get canned carriers and canned geometry. */
static bool is_deception(uint8_t scenario) {
    return scenario == MrdScenarioStatic || scenario == MrdScenarioCarry ||
           scenario == MrdScenarioCapture;
}

static uint8_t build_sky(MrdSim* s, SimSat* out) {
    bool live = s->epoch >= MRD_SIM_TAKEOVER;
    bool spoofed = is_deception(s->scenario) && live;
    bool frozen_sky = spoofed;
    bool jam = (s->scenario == MrdScenarioJamming) && live;

    uint8_t n = 0;
    for(uint8_t i = 0; i < SIM_SKY_N; i++) {
        /* Elevation walks at its own rate unless a canned sky has taken over,
         * in which case it is pinned to whatever it was at the takeover. */
        uint32_t t_s = frozen_sky ? MRD_SIM_TAKEOVER : s->epoch;
        int elev = (int)SIM_SKY[i].elev0 + ((int)SIM_SKY[i].rate10 * (int)t_s) / 600;
        if(elev < 3) elev = 3;
        if(elev > 88) elev = 88;

        int snr;
        if(jam) {
            /* The floor comes up; everything that was 40 dB-Hz is now 14. */
            snr = 11 + rnd_span(&s->rng, 4);
        } else if(spoofed) {
            /*
             * One transmitter, one power amplifier, one place on the ground.
             * Every fake channel arrives at nearly the same level, hot enough
             * to beat the real thing, and if anything the relationship with
             * elevation inverts slightly, because the spoofer is down near the
             * horizon rather than overhead. Worked in tenths of a dB so the
             * spread stays realistically narrow.
             */
            snr = 522 - elev / 4 + rnd_span(&s->rng, 4);
            snr /= 10;
        } else {
            /* Open sky: power climbs with elevation, plus a couple of dB of
             * honest scintillation and multipath. */
            snr = 29 + (elev * 22) / 100 + rnd_span(&s->rng, 2);
        }
        if(snr < 0) snr = 0;
        if(snr > 54) snr = 54;

        out[n].prn = SIM_SKY[i].prn;
        out[n].elev = (uint8_t)elev;
        out[n].az = SIM_SKY[i].az;
        out[n].snr = (uint8_t)snr;
        n++;
    }
    return n;
}

/* -------------------------------------------------------------- steps -- */

void mrd_sim_init(MrdSim* s, MrdScenario scenario, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->scenario = (uint8_t)scenario;
    s->rng = seed ? seed : 0xA5A5F00Du;
    s->epoch = 0;
    s->utc_ms = 12u * 3600u * 1000u; /* midday, so nothing wraps mid-demo */
    s->lat = SIM_LAT;
    s->lon = SIM_LON;
    s->alt = SIM_ALT;
    s->sog = 0.0f;
    s->cog = 0.0f;
    s->locked = true;
}

uint32_t mrd_sim_mono_ms(const MrdSim* s) {
    return s->epoch * 1000u;
}

bool mrd_sim_is_attack(MrdScenario scenario) {
    return scenario != MrdScenarioClean && scenario != MrdScenarioDrive;
}

/** Move the simulated receiver on by one second. */
static void advance_state(MrdSim* s) {
    bool live = s->epoch >= MRD_SIM_TAKEOVER;

    /* Time. Only the time attack decouples it from the local clock. */
    if(s->scenario == MrdScenarioTime && live) {
        s->utc_ms += 1500u; /* the receiver's clock is being pushed forward */
    } else {
        s->utc_ms += 1000u;
    }

    /* A metre or less of noise on each axis, which is what a stationary
     * consumer receiver actually does. Deliberately small enough that the
     * position-derived speed stays clear of the velocity threshold. */
    double jitter_lat = (double)rnd_span(&s->rng, 80) * MRD_D(0.0000001);
    double jitter_lon = (double)rnd_span(&s->rng, 80) * MRD_D(0.00000016);

    s->locked = true;
    s->alt = SIM_ALT + (float)rnd_span(&s->rng, 30) / 10.0f;

    switch(s->scenario) {
    case MrdScenarioDrive: {
        /* Due north-east at 13 m/s, with the Doppler figure agreeing. */
        const double step = MRD_D(13);
        s->lat += (step * MRD_D(0.7071)) / MRD_D(111320);
        s->lon += (step * MRD_D(0.7071)) / (MRD_D(111320) * MRD_D(0.6225)); /* cos(51.48) */
        s->sog = 13.0f;
        s->cog = 45.0f;
        s->lat += jitter_lat;
        s->lon += jitter_lon;
        break;
    }

    case MrdScenarioStatic:
        if(live) {
            /* Dragged to the false spot in one step, then held there exactly.
             * No jitter at all: the coordinate is being recited. */
            s->lat = SPOOF_LAT;
            s->lon = SPOOF_LON;
            s->alt = 0.0f;
            s->sog = 0.0f;
        } else {
            s->lat = SIM_LAT + jitter_lat;
            s->lon = SIM_LON + jitter_lon;
            s->sog = (float)(xs32(&s->rng) % 3u) / 10.0f;
        }
        break;

    case MrdScenarioCarry:
        if(live) {
            /*
             * The quiet version. Nobody teleports; the fix is walked away at
             * 9 m/s while speed over ground keeps insisting you are parked.
             * No single position step is remarkable - only the disagreement
             * between the two independent measurements gives it away.
             */
            s->lat += MRD_D(9) / MRD_D(111320);
            s->sog = 0.0f;
        } else {
            s->lat = SIM_LAT + jitter_lat;
            s->lon = SIM_LON + jitter_lon;
            s->sog = 0.1f;
        }
        break;

    case MrdScenarioCapture:
        if(s->epoch >= MRD_SIM_TAKEOVER && s->epoch < MRD_SIM_TAKEOVER + 4) {
            s->locked = false; /* suppressed, waiting for the receiver to give up */
        } else if(s->epoch >= MRD_SIM_TAKEOVER + 4) {
            s->lat = SPOOF_LAT + jitter_lat;
            s->lon = SPOOF_LON + jitter_lon;
            s->sog = 0.2f;
        } else {
            s->lat = SIM_LAT + jitter_lat;
            s->lon = SIM_LON + jitter_lon;
            s->sog = 0.1f;
        }
        break;

    case MrdScenarioJamming:
        s->lat = SIM_LAT + jitter_lat;
        s->lon = SIM_LON + jitter_lon;
        s->sog = 0.1f;
        if(live) s->locked = false;
        break;

    case MrdScenarioTime:
    case MrdScenarioClean:
    default:
        s->lat = SIM_LAT + jitter_lat;
        s->lon = SIM_LON + jitter_lon;
        s->sog = (float)(xs32(&s->rng) % 3u) / 10.0f;
        break;
    }

    s->epoch++;
}

void mrd_sim_step(MrdSim* s, MrdSimEmit emit, void* context) {
    advance_state(s);

    SimSat sky[SIM_SKY_N];
    uint8_t nsat = build_sky(s, sky);

    bool spoofed = is_deception(s->scenario) && (s->epoch >= MRD_SIM_TAKEOVER);

    char line[110];
    char tbuf[16], latb[16], lonb[16], nb1[12], nb2[12];
    char lath, lonh;

    fmt_time(tbuf, sizeof(tbuf), s->utc_ms);
    fmt_deg(latb, sizeof(latb), s->lat, false, &lath);
    fmt_deg(lonb, sizeof(lonb), s->lon, true, &lonh);

    uint8_t used = s->locked ? (uint8_t)(nsat > 9 ? 9 : nsat) : 0;

    /* HDOP: normally jitters with the geometry. A canned sky recites one
     * value, which is the tell the DOP check is looking for. */
    float hdop;
    if(spoofed) {
        hdop = 0.90f;
    } else {
        hdop = 0.80f + (float)(xs32(&s->rng) % 60u) / 100.0f;
    }

    /* ---- GGA ---- */
    fmt_num(nb1, sizeof(nb1), hdop, 2);
    fmt_num(nb2, sizeof(nb2), s->alt, 1);
    snprintf(
        line,
        sizeof(line),
        "$GPGGA,%s,%s,%c,%s,%c,%d,%02d,%s,%s,M,47.0,M,,",
        tbuf,
        latb,
        lath,
        lonb,
        lonh,
        s->locked ? 1 : 0,
        used,
        nb1,
        nb2);
    put_checksum(line, sizeof(line));
    emit(context, line);

    /* ---- RMC ---- */
    fmt_num(nb1, sizeof(nb1), s->sog / 0.514444f, 1); /* m/s back to knots */
    fmt_num(nb2, sizeof(nb2), s->cog, 1);
    snprintf(
        line,
        sizeof(line),
        "$GPRMC,%s,%c,%s,%c,%s,%c,%s,%s,040826,,",
        tbuf,
        s->locked ? 'A' : 'V',
        latb,
        lath,
        lonb,
        lonh,
        s->sog > 0.0f ? nb1 : "0.0",
        nb2);
    put_checksum(line, sizeof(line));
    emit(context, line);

    /* ---- GSA ---- */
    {
        char prns[64];
        size_t off = 0;
        prns[0] = '\0';
        for(uint8_t i = 0; i < 12; i++) {
            char one[8];
            if(i < used) {
                snprintf(one, sizeof(one), "%02u,", (unsigned)sky[i].prn);
            } else {
                snprintf(one, sizeof(one), ",");
            }
            size_t l = strlen(one);
            if(off + l < sizeof(prns)) {
                memcpy(prns + off, one, l + 1);
                off += l;
            }
        }
        fmt_num(nb1, sizeof(nb1), hdop, 2);
        snprintf(
            line,
            sizeof(line),
            "$GPGSA,A,%d,%s%s,%s,2.1",
            s->locked ? 3 : 1,
            prns,
            "1.4",
            nb1);
        put_checksum(line, sizeof(line));
        emit(context, line);
    }

    /* ---- GSV ---- */
    {
        uint8_t msgs = (uint8_t)((nsat + 3) / 4);
        for(uint8_t m = 0; m < msgs; m++) {
            size_t off = (size_t)snprintf(
                line,
                sizeof(line),
                "$GPGSV,%u,%u,%02u",
                (unsigned)msgs,
                (unsigned)(m + 1),
                (unsigned)nsat);
            for(uint8_t k = 0; k < 4; k++) {
                uint8_t i = (uint8_t)(m * 4 + k);
                if(i >= nsat || off >= sizeof(line) - 4) break;
                off += (size_t)snprintf(
                    line + off,
                    sizeof(line) - off,
                    ",%02u,%02u,%03u,%02u",
                    (unsigned)sky[i].prn,
                    (unsigned)sky[i].elev,
                    (unsigned)sky[i].az,
                    (unsigned)sky[i].snr);
            }
            put_checksum(line, sizeof(line));
            emit(context, line);
        }
    }
}

/* ---------------------------------------------------------- labelling -- */

const char* mrd_sim_name(MrdScenario scenario) {
    switch(scenario) {
    case MrdScenarioClean:
        return "Open sky";
    case MrdScenarioDrive:
        return "Driving";
    case MrdScenarioStatic:
        return "Held in place";
    case MrdScenarioCarry:
        return "Carried off";
    case MrdScenarioTime:
        return "Clock pushed";
    case MrdScenarioCapture:
        return "Lock captured";
    case MrdScenarioJamming:
        return "Jammed";
    default:
        return "?";
    }
}

const char* mrd_sim_blurb(MrdScenario scenario) {
    switch(scenario) {
    case MrdScenarioClean:
        return "A stationary receiver under open sky. This is what an honest "
               "reading looks like, and every check should stay quiet.";
    case MrdScenarioDrive:
        return "Thirteen metres a second, north-east. Movement on its own must "
               "never look like an attack.";
    case MrdScenarioStatic:
        return "After eight seconds the fix is dragged four kilometres and "
               "pinned. Loud, flat carriers; a sky that stops moving.";
    case MrdScenarioCarry:
        return "The quiet one. The position walks away at nine metres a second "
               "while Doppler still reports parked. Only the disagreement "
               "gives it away.";
    case MrdScenarioTime:
        return "Position stays honest, the clock does not - pushed on at 1.5 "
               "seconds per second. This is the attack on timing, not location.";
    case MrdScenarioCapture:
        return "Lock is suppressed for four seconds, then handed back four "
               "kilometres away. The textbook takeover.";
    case MrdScenarioJamming:
        return "Not deception - denial. The noise floor comes up, carriers "
               "collapse, the fix dies. Reported separately, because it is a "
               "different attack.";
    default:
        return "";
    }
}
