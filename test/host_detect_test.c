/*
 * Engine tests.
 *
 * A detector nobody has tried to fool is a decoration. Each scenario below
 * runs synthesised NMEA through the real parser into the real engine - the
 * same path the UART takes on the device - and asserts two things that matter
 * equally: that the attacks are caught, and that the honest sky is left alone.
 *
 * The false-positive half is the harder half. A GPS integrity monitor that
 * cries spoof on an ordinary drive is worse than no monitor at all, because
 * the one time it is right nobody will believe it.
 */
#include "../helpers/mrd_detect.h"
#include "../helpers/mrd_nmea.h"
#include "../helpers/mrd_sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks = 0, failures = 0;

static void ok(bool cond, const char* what) {
    checks++;
    if(!cond) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void near(double got, double want, double tol, const char* what) {
    checks++;
    if(fabs(got - want) > tol) {
        failures++;
        printf("  FAIL  %s: got %.3f want %.3f\n", what, got, want);
    }
}

/* ============================================================== the rig -- */

typedef struct {
    MrdNmea nmea;
    MrdDetect det;
    MrdSim sim;
    uint32_t epochs;
} Rig;

static void rig_emit(void* context, const char* line) {
    Rig* r = context;
    uint8_t upd = mrd_nmea_feed(&r->nmea, line);
    if(upd & MrdUpdEpoch) {
        mrd_detect_epoch(
            &r->det,
            &r->nmea.epoch,
            r->nmea.sats,
            r->nmea.sat_count,
            mrd_sim_mono_ms(&r->sim));
        r->epochs++;
    }
}

static void rig_run(Rig* r, MrdScenario sc, uint32_t seconds, MrdSensitivity sens) {
    memset(r, 0, sizeof(*r));
    mrd_nmea_init(&r->nmea);
    mrd_detect_init(&r->det, sens, 60);
    mrd_sim_init(&r->sim, sc, 0xBEEF1234u);

    for(uint32_t i = 0; i < seconds; i++) mrd_sim_step(&r->sim, rig_emit, r);

    if(mrd_nmea_flush(&r->nmea)) {
        mrd_detect_epoch(
            &r->det,
            &r->nmea.epoch,
            r->nmea.sats,
            r->nmea.sat_count,
            mrd_sim_mono_ms(&r->sim));
        r->epochs++;
    }
}

/** Did this check ever flag during the run, latched or not? */
static bool fired(const Rig* r, MrdCheckId id) {
    return r->det.checks[id].hits > 0;
}

static void report(const Rig* r, const char* label) {
    printf(
        "  %-14s %-12s score %3u  armed %2u  alerts %u  warns %u%s\n",
        label,
        mrd_verdict_name((MrdVerdict)r->det.verdict),
        r->det.score,
        r->det.armed,
        r->det.alerts,
        r->det.warns,
        r->det.jamming ? "  [JAMMING]" : "");

    for(uint8_t i = 0; i < MrdCheckCount; i++) {
        const MrdCheck* c = &r->det.checks[i];
        if(c->hits == 0) continue;
        char obs[64];
        mrd_check_observed(&r->det, (MrdCheckId)i, obs, sizeof(obs));
        printf(
            "      %-22s %-5s x%-4u %s\n",
            mrd_check_name((MrdCheckId)i),
            mrd_state_name((MrdCheckState)c->state),
            c->hits,
            obs);
    }
}

/* ============================================================= geodesy -- */

static void test_geodesy(void) {
    printf("geodesy\n");

    /* Exact on the sphere the implementation uses: R * pi/180 per degree. */
    const double R = 6371008.8;
    const double per_deg = R * 3.14159265358979323846 / 180.0;

    near(mrd_geo_distance_m(0, 0, 0, 1), per_deg, 0.5, "one degree of longitude at the equator");
    near(mrd_geo_distance_m(0, 0, 1, 0), per_deg, 0.5, "one degree of latitude");
    near(mrd_geo_distance_m(0, 0, 90, 0), per_deg * 90.0, 5.0, "pole is a quarter circumference");
    near(mrd_geo_distance_m(0, 0, 0, 180), per_deg * 180.0, 10.0, "antipode is half");
    near(mrd_geo_distance_m(51.5, -0.1, 51.5, -0.1), 0.0, 1e-6, "same point is zero");

    /* Longitude converges with latitude: at 60 degrees north a degree of
     * longitude is half what it is at the equator. */
    near(
        mrd_geo_distance_m(60, 0, 60, 1),
        per_deg * 0.5,
        200.0,
        "longitude converges toward the pole");

    float e, n;
    mrd_geo_offset_m(51.4779, -0.0015, 51.4779 + 1.0, -0.0015, &e, &n);
    near(n, per_deg, 1.0, "offset north matches a degree of latitude");
    near(e, 0.0, 0.001, "offset east is zero on the same meridian");

    /* The two functions have to agree at the scale the trail plot works at. */
    double lat2 = 51.4779 + 100.0 / per_deg;
    mrd_geo_offset_m(51.4779, -0.0015, lat2, -0.0015, &e, &n);
    near(n, 100.0, 0.1, "offset agrees with distance over 100 m");
    near(mrd_geo_distance_m(51.4779, -0.0015, lat2, -0.0015), 100.0, 0.1, "distance over 100 m");
}

/* ======================================================= honest skies -- */

static void test_clean(void) {
    printf("honest sky must stay quiet\n");

    Rig r;
    rig_run(&r, MrdScenarioClean, 600, MrdSensNormal);
    report(&r, "open sky");

    ok(r.epochs >= 595, "every second produced an epoch");
    ok(r.det.verdict == MrdVerdictNominal, "ten minutes of open sky reads NOMINAL");
    ok(r.det.alerts == 0, "no check is alerting");
    ok(r.det.score < 20, "score stays in the nominal band");
    ok(r.det.armed >= 9, "nearly every check had enough data to run");
    ok(!r.det.jamming, "no jamming reported");

    /* The individual tells, each of which would be a false accusation. */
    ok(!fired(&r, MrdCheckJump), "no phantom teleport");
    ok(!fired(&r, MrdCheckFrozen), "natural jitter is not read as frozen");
    ok(!fired(&r, MrdCheckSnrFlat), "real carrier spread is not read as flat");
    ok(!fired(&r, MrdCheckSnrHot), "open-sky power is not read as hot");
    ok(!fired(&r, MrdCheckSnrElev), "power still rises with elevation");
    ok(!fired(&r, MrdCheckClock), "clock tracks the local clock");
    ok(!fired(&r, MrdCheckSkyStatic), "the sky is seen to move");
    ok(!fired(&r, MrdCheckDop), "varying DOP is not read as canned");
    ok(!fired(&r, MrdCheckCapture), "lock is never reported captured");

    ok(r.det.snr_sigma > 2.0f, "carrier spread measured above the flat threshold");
    ok(r.det.snr_elev_r > 0.4f, "power/elevation correlation is strongly positive");
    ok(r.det.snr_mean > 30.0f && r.det.snr_mean < 47.0f, "mean C/N0 is plausible");

    /* And the same, moving. Motion on its own must never look like an attack. */
    rig_run(&r, MrdScenarioDrive, 300, MrdSensNormal);
    report(&r, "driving");
    ok(r.det.verdict == MrdVerdictNominal, "driving reads NOMINAL");
    ok(r.det.alerts == 0, "driving raises no alert");
    ok(!fired(&r, MrdCheckJump), "13 m/s is not a teleport");
    ok(!fired(&r, MrdCheckVelocity), "Doppler agrees with the track while moving");

    /* Even at the most paranoid setting, an honest sky must not be accused. */
    rig_run(&r, MrdScenarioClean, 400, MrdSensHigh);
    report(&r, "open sky/high");
    ok(r.det.verdict <= MrdVerdictNominal, "open sky survives high sensitivity");
    ok(r.det.alerts == 0, "high sensitivity raises no alert on open sky");
}

/* ============================================================= attacks -- */

static void test_static_spoof(void) {
    printf("held in place\n");

    Rig r;
    rig_run(&r, MrdScenarioStatic, 400, MrdSensNormal);
    report(&r, "static spoof");

    ok(r.det.verdict == MrdVerdictLikely, "reads SPOOF LIKELY");
    ok(r.det.score >= 65, "score is in the top band");
    ok(fired(&r, MrdCheckJump), "the drag to the false position is caught");
    ok(fired(&r, MrdCheckFrozen), "the recited coordinate is caught");
    ok(fired(&r, MrdCheckSnrFlat), "flat carrier power is caught");
    ok(fired(&r, MrdCheckSnrHot), "excessive carrier power is caught");
    ok(fired(&r, MrdCheckSkyStatic), "the frozen constellation is caught");
    ok(fired(&r, MrdCheckDop), "the canned DOP is caught");
    ok(r.det.alerts >= 4, "several independent checks agree");
}

static void test_carry_off(void) {
    printf("carried off\n");

    Rig r;
    rig_run(&r, MrdScenarioCarry, 120, MrdSensNormal);
    report(&r, "carry-off");

    /* The point of this one: no single position step is remarkable, so the
     * teleport check never fires. Only comparing two independent measurements
     * of the same quantity finds it. */
    ok(!fired(&r, MrdCheckJump), "9 m/s is below any teleport threshold");
    ok(fired(&r, MrdCheckVelocity), "Doppler/track disagreement is caught");
    ok(r.det.verdict >= MrdVerdictSuspect, "reads SUSPECT or worse");
}

static void test_time_push(void) {
    printf("clock pushed\n");

    Rig r;
    rig_run(&r, MrdScenarioTime, 120, MrdSensNormal);
    report(&r, "time push");

    ok(fired(&r, MrdCheckClock), "clock drift is caught");
    ok(r.det.clock_drift_s > 15.0f, "drift accumulated as expected");
    ok(r.det.verdict >= MrdVerdictSuspect, "a clock-only attack still reads SUSPECT");

    /* The sky is untouched in this scenario, and must be reported as such.
     * Naming the one thing that is wrong is the entire value of the screen. */
    ok(!fired(&r, MrdCheckSnrFlat), "carrier spread stays honest");
    ok(!fired(&r, MrdCheckSnrHot), "carrier power stays honest");
    ok(!fired(&r, MrdCheckFrozen), "position stays honest");
    ok(!fired(&r, MrdCheckJump), "position stays honest under a time attack");
}

static void test_capture(void) {
    printf("lock captured\n");

    Rig r;
    rig_run(&r, MrdScenarioCapture, 200, MrdSensNormal);
    report(&r, "capture");

    ok(fired(&r, MrdCheckCapture), "the takeover across the outage is caught");
    ok(!fired(&r, MrdCheckJump), "a jump across an outage is not a teleport");
    ok(r.det.verdict >= MrdVerdictSuspect, "reads SUSPECT or worse");
}

static void test_jamming(void) {
    printf("jamming is not spoofing\n");

    Rig r;
    rig_run(&r, MrdScenarioJamming, 120, MrdSensNormal);
    report(&r, "jamming");

    ok(r.det.jamming, "jamming is reported");
    ok(r.det.jam_reason != MrdJamNone, "with a reason");
    /* Denial and deception are different attacks and get different answers.
     * Calling a jammer a spoofer would be a lie in the user's face. */
    ok(r.det.verdict < MrdVerdictSuspect, "jamming does not read as spoofing");
}

/* ========================================================= engine unit -- */

/*
 * Straight into the engine with hand-built epochs, so latch and decay can be
 * driven exactly rather than waited for.
 */
static void feed(MrdDetect* d, double lat, double lon, uint32_t sec, uint8_t nsat) {
    MrdFix f;
    memset(&f, 0, sizeof(f));
    f.valid = true;
    f.lat = lat;
    f.lon = lon;
    f.quality = 1;
    f.fix_type = 3;
    f.sats_used = nsat;
    f.sats_view = nsat;
    f.hdop = 0.8f + (float)(sec % 7u) / 10.0f;
    f.utc_ms = 43200000u + sec * 1000u;
    f.alt_m = 45.0f;
    f.has_alt = true;
    f.speed_mps = 0.0f;
    f.has_speed = true;

    MrdSat sats[8];
    memset(sats, 0, sizeof(sats));
    for(uint8_t i = 0; i < 8; i++) {
        sats[i].prn = (uint8_t)(i + 1);
        sats[i].sys = MrdSysGps;
        sats[i].elev = (uint8_t)(10 + i * 9 + (sec % 3u));
        sats[i].azim = (uint16_t)(i * 45);
        sats[i].snr = (uint8_t)(30 + i * 2);
        sats[i].used = true;
    }
    mrd_detect_epoch(d, &f, sats, 8, sec * 1000u);
}

static void test_latch_and_decay(void) {
    printf("latch and decay\n");

    MrdDetect d;
    mrd_detect_init(&d, MrdSensNormal, 30);

    /* Two ordinary epochs, then a jump of five degrees of latitude in one
     * second - roughly 550 km, or Mach 1600. */
    feed(&d, 51.4779, -0.0015, 1, 9);
    feed(&d, 51.47791, -0.00151, 2, 9);
    ok(d.checks[MrdCheckJump].state == MrdStateOk, "no jump yet");

    feed(&d, 56.4779, -0.0015, 3, 9);
    ok(d.checks[MrdCheckJump].state == MrdStateAlert, "the teleport alerts");
    ok(d.score >= 55, "one unambiguous alert floors the score");

    /* It has to stay up long enough to be read. */
    for(uint32_t s = 4; s < 25; s++) feed(&d, 56.4779 + (double)s * 1e-7, -0.0015, s, 9);
    ok(d.checks[MrdCheckJump].state == MrdStateAlert, "the alert is still latched 20 s later");

    /* And then it has to come back down, or the screen would never recover. */
    for(uint32_t s = 25; s < 45; s++) feed(&d, 56.4779 + (double)s * 1e-7, -0.0015, s, 9);
    ok(d.checks[MrdCheckJump].state == MrdStateOk, "the alert decays after the hold expires");
    ok(d.checks[MrdCheckJump].hits == 1, "and is remembered as having happened once");

    /* A reset has to clear the session outright. */
    mrd_detect_reset(&d);
    ok(d.score == 0, "reset clears the score");
    ok(d.armed == 0, "reset disarms every check");
    ok(d.checks[MrdCheckJump].hits == 0, "reset clears the history");
    ok(d.verdict == MrdVerdictNoSignal, "reset returns to NO SIGNAL");
}

static void test_frozen_needs_a_window(void) {
    printf("frozen position window\n");

    MrdDetect d;
    mrd_detect_init(&d, MrdSensNormal, 60);

    /* One repeated coordinate is not evidence of anything. */
    feed(&d, 51.4779, -0.0015, 1, 9);
    feed(&d, 51.4779, -0.0015, 2, 9);
    feed(&d, 51.4779, -0.0015, 3, 9);
    ok(d.checks[MrdCheckFrozen].state != MrdStateAlert, "three identical fixes is not enough");

    for(uint32_t s = 4; s < 30; s++) feed(&d, 51.4779, -0.0015, s, 9);
    ok(d.checks[MrdCheckFrozen].state == MrdStateAlert, "a full window of them is");

    /* Under four satellites the receiver may be repeating a last-known fix
     * for entirely honest reasons, and must not be accused. */
    mrd_detect_init(&d, MrdSensNormal, 60);
    for(uint32_t s = 1; s < 30; s++) feed(&d, 51.4779, -0.0015, s, 3);
    ok(d.checks[MrdCheckFrozen].state != MrdStateAlert, "a 3-satellite repeat is not accused");
}

static void test_sensitivity(void) {
    printf("sensitivity\n");

    Rig lo, mid, hi;
    rig_run(&lo, MrdScenarioCarry, 90, MrdSensLow);
    rig_run(&mid, MrdScenarioCarry, 90, MrdSensNormal);
    rig_run(&hi, MrdScenarioCarry, 90, MrdSensHigh);

    printf("      low %u  normal %u  high %u\n", lo.det.score, mid.det.score, hi.det.score);
    ok(hi.det.score >= mid.det.score, "high sensitivity scores at least as high as normal");
    ok(mid.det.score >= lo.det.score, "normal scores at least as high as low");

    /* The setting must not be able to turn a real attack into a clean bill. */
    ok(lo.det.verdict >= MrdVerdictAnomalous, "even the least paranoid setting notices");
}

/* ============================================================ coverage -- */

/*
 * The two claims Meridian must never be caught making: that one observation is
 * proof, and that it is certain. Both are structural, so both are tested
 * structurally rather than hoped for.
 */
static void test_honesty_invariants(void) {
    printf("honesty invariants\n");

    /* A lone check, alerting flat out for a long time, must not be able to
     * reach SPOOF LIKELY. Driven through the clock check, which is the one
     * that can fire entirely on its own with the sky untouched. */
    Rig r;
    rig_run(&r, MrdScenarioTime, 600, MrdSensHigh);
    ok(r.det.families == 1, "one family alerting");
    ok(r.det.verdict < MrdVerdictLikely, "a single family never reaches SPOOF LIKELY");
    ok(r.det.score < 65, "and its score stays below that band");

    /* Not even a total, unambiguous, everything-wrong spoof reads as certain.
     * A single antenna cannot prove this, and the number says so. */
    rig_run(&r, MrdScenarioStatic, 600, MrdSensHigh);
    report(&r, "worst case");
    ok(r.det.verdict == MrdVerdictLikely, "an all-families spoof does read SPOOF LIKELY");
    ok(r.det.score < 100, "but the score never claims certainty");
    ok(r.det.score <= 96, "the ceiling holds");
    ok(r.det.families >= 3, "three or more independent paths agree");
}

static void test_presentation(void) {
    printf("presentation\n");

    /* Every check owes the user a name, an explanation and an honest benign
     * cause. A silent row on the evidence screen is a bug. */
    for(uint8_t i = 0; i < MrdCheckCount; i++) {
        ok(strlen(mrd_check_name((MrdCheckId)i)) > 1, "check has a name");
        ok(strlen(mrd_check_title((MrdCheckId)i)) > 8, "check has a title");
        ok(strlen(mrd_check_what((MrdCheckId)i)) > 40, "check explains itself");
        ok(strlen(mrd_check_benign((MrdCheckId)i)) > 40, "check offers a benign cause");
        ok(strlen(mrd_check_name((MrdCheckId)i)) <= 22, "the name fits a 128 px row");
    }

    for(uint8_t v = 0; v < MrdVerdictCount; v++) {
        ok(strlen(mrd_verdict_name((MrdVerdict)v)) > 2, "verdict has a name");
        ok(strlen(mrd_verdict_name((MrdVerdict)v)) <= 12, "verdict fits the header");
    }

    for(uint8_t s = 0; s < MrdScenarioCount; s++) {
        ok(strlen(mrd_sim_name((MrdScenario)s)) > 2, "scenario has a name");
        ok(strlen(mrd_sim_blurb((MrdScenario)s)) > 40, "scenario explains itself");
    }

    /* Before a check has run, it must say so rather than show a zero. */
    MrdDetect d;
    mrd_detect_init(&d, MrdSensNormal, 60);
    char buf[64];
    mrd_check_observed(&d, MrdCheckJump, buf, sizeof(buf));
    ok(strstr(buf, "not enough data") != NULL, "unarmed checks say so");
}

int main(void) {
    printf("Meridian - integrity engine\n\n");

    test_geodesy();
    test_clean();
    test_static_spoof();
    test_carry_off();
    test_time_push();
    test_capture();
    test_jamming();
    test_latch_and_decay();
    test_frozen_needs_a_window();
    test_sensitivity();
    test_honesty_invariants();
    test_presentation();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
