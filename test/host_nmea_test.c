/*
 * Parser tests.
 *
 * Every sentence below is a published NMEA 0183 example with its own original
 * checksum left intact, so the checksum implementation is validated against
 * vectors it did not produce. If the parser ever starts agreeing with itself
 * instead of with the standard, these stop passing.
 */
#include "../helpers/mrd_nmea.h"

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
        printf("  FAIL  %s: got %.6f want %.6f\n", what, got, want);
    }
}

/* ------------------------------------------------------------ vectors -- */

static const char* const GGA = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char* const RMC =
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
static const char* const GSA = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39";
static const char* const GSV =
    "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75";
static const char* const GSV_UNTRACKED =
    "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74";
static const char* const GNGSA = "$GNGSA,A,3,80,71,73,79,69,,,,,,,,1.83,1.09,1.47*17";
static const char* const VTG = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";
static const char* const GLL = "$GPGLL,4916.45,N,12311.12,W,225444,A,*1D";

/* ---------------------------------------------------------- checksums -- */

static void test_checksum(void) {
    printf("checksum\n");

    struct {
        const char* s;
        uint8_t want;
    } v[] = {
        {GGA, 0x47},
        {RMC, 0x6A},
        {GSA, 0x39},
        {GSV, 0x75},
        {GNGSA, 0x17},
        {VTG, 0x48},
        {GLL, 0x1D},
    };

    for(size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        const char* star = strchr(v[i].s, '*');
        uint8_t got = mrd_nmea_checksum(v[i].s + 1, (size_t)(star - v[i].s) - 1);
        ok(got == v[i].want, "published checksum reproduced");
    }

    /* A sentence whose checksum does not match must be dropped, not repaired. */
    MrdNmea n;
    mrd_nmea_init(&n);
    uint8_t r = mrd_nmea_feed(
        &n, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48");
    ok(r == MrdUpdRejected, "bad checksum rejected");
    ok(n.bad_checksum == 1, "bad checksum counted");
    ok(n.sentences == 0, "bad checksum not counted as accepted");

    /* No checksum at all is accepted: plenty of receivers and most simulators
     * omit it, and refusing them would make the app useless on working kit. */
    mrd_nmea_init(&n);
    r = mrd_nmea_feed(&n, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    ok((r & MrdUpdPosition) != 0, "missing checksum accepted");
}

/* -------------------------------------------------------------- units -- */

static void test_fields(void) {
    printf("field conversion\n");

    double d;
    ok(mrd_parse_latlon("4807.038", 'N', &d), "lat parses");
    near(d, 48.0 + 7.038 / 60.0, 1e-9, "ddmm.mmm to degrees");

    ok(mrd_parse_latlon("01131.000", 'E', &d), "lon parses");
    near(d, 11.0 + 31.0 / 60.0, 1e-9, "dddmm.mmm to degrees");

    ok(mrd_parse_latlon("4807.038", 'S', &d), "south parses");
    near(d, -(48.0 + 7.038 / 60.0), 1e-9, "south is negative");

    ok(mrd_parse_latlon("12311.12", 'W', &d), "west parses");
    near(d, -(123.0 + 11.12 / 60.0), 1e-9, "west is negative");

    ok(!mrd_parse_latlon("4807.038", 'X', &d), "bad hemisphere rejected");
    ok(!mrd_parse_latlon("4867.038", 'N', &d), "minutes >= 60 rejected");
    ok(!mrd_parse_latlon("", 'N', &d), "empty rejected");

    uint32_t ms;
    ok(mrd_parse_time("123519", &ms), "hhmmss parses");
    ok(ms == (12u * 3600u + 35u * 60u + 19u) * 1000u, "hhmmss to ms");
    ok(mrd_parse_time("123519.50", &ms), "fractional seconds parse");
    ok(ms == (12u * 3600u + 35u * 60u + 19u) * 1000u + 500u, "fraction carried");
    ok(mrd_parse_time("235960", &ms), "leap second accepted");
    ok(!mrd_parse_time("243000", &ms), "hour 24 rejected");
    ok(!mrd_parse_time("12351", &ms), "short time rejected");

    /* The frozen-position check compares parsed doubles for exact equality,
     * so identical text has to produce identical bits every single time. */
    double a = 0.0, b = 1.0;
    ok(mrd_parse_double("51.4779123", &a) && mrd_parse_double("51.4779123", &b), "doubles parse");
    ok(a == b, "identical text gives identical bits");
    ok(!mrd_parse_double("1.2.3", &a), "trailing junk rejected");
    ok(!mrd_parse_double("abc", &a), "non-numeric rejected");
    ok(mrd_parse_double("-0.5", &a) && a == -0.5, "negative parses");
    ok(mrd_parse_double(".25", &a) && a == 0.25, "leading point parses");
}

/* ---------------------------------------------------------- sentences -- */

static void test_gga_rmc(void) {
    printf("GGA / RMC\n");

    MrdNmea n;
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GGA);

    near(n.fix.lat, 48.1173, 1e-4, "GGA latitude");
    near(n.fix.lon, 11.51667, 1e-4, "GGA longitude");
    ok(n.fix.quality == 1, "GGA fix quality");
    ok(n.fix.valid, "GGA quality 1 means valid");
    ok(n.fix.sats_used == 8, "GGA satellites used");
    near(n.fix.hdop, 0.9, 1e-5, "GGA HDOP");
    near(n.fix.alt_m, 545.4, 1e-3, "GGA altitude");
    ok(n.fix.has_alt, "GGA altitude present");

    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, RMC);
    ok(n.fix.valid, "RMC status A means valid");
    near(n.fix.speed_mps, 22.4 * 0.514444, 1e-3, "RMC knots to m/s");
    near(n.fix.course_deg, 84.4, 1e-3, "RMC course");
    ok(n.fix.date == 230394, "RMC date");

    /* Status V is a warning flag: position is unusable even if fields exist. */
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, "$GPRMC,123519,V,4807.038,N,01131.000,E,,,230394,,");
    ok(!n.fix.valid, "RMC status V is not a fix");

    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GGA);
    mrd_nmea_feed(&n, VTG);
    near(n.fix.speed_mps, 10.2 / 3.6, 1e-3, "VTG km/h to m/s");

    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GLL);
    ok(n.fix.valid, "GLL status A means valid");
    near(n.fix.lat, 49.274166, 1e-5, "GLL latitude");
    near(n.fix.lon, -123.185333, 1e-5, "GLL longitude");
}

static void test_gsa_gsv(void) {
    printf("GSA / GSV\n");

    MrdNmea n;
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GSA);

    ok(n.fix.fix_type == 3, "GSA 3D fix");
    near(n.fix.pdop, 2.5, 1e-5, "GSA PDOP");
    near(n.fix.hdop, 1.3, 1e-5, "GSA HDOP");
    near(n.fix.vdop, 2.1, 1e-5, "GSA VDOP");
    ok(n.sat_count == 5, "GSA names five satellites");

    uint8_t used = 0;
    for(uint8_t i = 0; i < n.sat_count; i++) {
        if(n.sats[i].used) used++;
    }
    ok(used == 5, "all GSA satellites marked used");

    /* NMEA 4.10 numbering: a combined-talker GSA identifies the constellation
     * by the satellite number itself. 65-96 is GLONASS, offset by 64. */
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GNGSA);
    ok(n.sat_count == 5, "GNGSA names five satellites");
    bool all_glonass = true;
    for(uint8_t i = 0; i < n.sat_count; i++) {
        if(n.sats[i].sys != MrdSysGlonass) all_glonass = false;
    }
    ok(all_glonass, "65-96 decoded as GLONASS");
    ok(n.sats[0].prn == 80 - 64, "GLONASS PRN offset removed");

    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GSV);
    ok(n.sat_count == 4, "GSV carries four satellites per message");
    ok(n.sats[0].prn == 1, "GSV PRN");
    ok(n.sats[0].elev == 40, "GSV elevation");
    ok(n.sats[0].azim == 83, "GSV azimuth");
    ok(n.sats[0].snr == 46, "GSV C/N0");
    ok(n.sats[0].sys == MrdSysGps, "GP talker means GPS");

    /* An empty or zero C/N0 field means in view but not tracked. It must not
     * be confused with a tracked satellite, or the SNR statistics are junk. */
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, GSV_UNTRACKED);
    ok(n.sat_count == 4, "untracked satellites still listed");
    uint8_t tracked = 0;
    for(uint8_t i = 0; i < n.sat_count; i++) {
        if(n.sats[i].snr > 0) tracked++;
    }
    ok(tracked == 0, "zero C/N0 counts as untracked");

    /* Message 1 restarts the series, dropping satellites the receiver has
     * since lost rather than leaving them on the sky plot forever. */
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, "$GPGSV,1,1,04,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45");
    ok(n.sat_count == 4, "first series populates");
    mrd_nmea_feed(&n, "$GPGSV,1,1,02,01,41,084,45,02,18,309,42");
    ok(n.sat_count == 2, "new series clears the old one");
    ok(n.sats[0].elev == 41, "new series values applied");

    /* Different constellations run independent series and must not wipe
     * each other. */
    mrd_nmea_init(&n);
    mrd_nmea_feed(&n, "$GPGSV,1,1,02,01,40,083,46,02,17,308,41");
    mrd_nmea_feed(&n, "$GLGSV,1,1,02,78,25,120,38,79,40,200,42");
    ok(n.sat_count == 4, "GPS and GLONASS coexist");
    mrd_nmea_feed(&n, "$GPGSV,1,1,01,01,42,085,44");
    ok(n.sat_count == 3, "restarting GPS leaves GLONASS alone");
}

/* ------------------------------------------------------------- epochs -- */

static void test_epochs(void) {
    printf("epoch assembly\n");

    MrdNmea n;
    mrd_nmea_init(&n);

    /* One second's burst. Nothing is published until the next second starts:
     * the timestamp changing is the only reliable end-of-burst marker NMEA
     * gives us. */
    ok(!(mrd_nmea_feed(&n, "$GPGGA,120000,4807.038,N,01131.000,E,1,08,0.9,100.0,M,46.9,M,,") &
         MrdUpdEpoch),
       "first burst does not publish");
    mrd_nmea_feed(&n, "$GPRMC,120000,A,4807.038,N,01131.000,E,000.0,000.0,040826,,");
    mrd_nmea_feed(&n, GSA);
    mrd_nmea_feed(&n, GSV);
    ok(n.epoch.utc_ms == MRD_UTC_UNKNOWN, "no epoch published yet");

    uint8_t r =
        mrd_nmea_feed(&n, "$GPGGA,120001,4807.039,N,01131.001,E,1,09,0.8,101.0,M,46.9,M,,");
    ok((r & MrdUpdEpoch) != 0, "new timestamp closes the previous epoch");
    ok(n.epoch.utc_ms == 12u * 3600u * 1000u, "published epoch carries its own time");
    ok(n.epoch.sats_used == 8, "published epoch carries the first burst's data");
    ok(n.epoch.sats_view == 8, "satellites in view from GSV");
    near(n.epoch.alt_m, 100.0, 1e-3, "published altitude is the first burst's");

    /* GSA's used flags are rebuilt every second, so a satellite dropped from
     * the solution must not stay marked. */
    mrd_nmea_feed(&n, "$GPGSA,A,3,04,,,,,,,,,,,,2.5,1.3,2.1");
    uint8_t used = 0;
    for(uint8_t i = 0; i < n.sat_count; i++) {
        if(n.sats[i].used) used++;
    }
    ok(used == 1, "used flags reset each epoch");

    /* Closing the stream must not lose the last second. */
    ok(mrd_nmea_flush(&n), "flush publishes the open epoch");
    ok(n.epoch.utc_ms == 12u * 3600u * 1000u + 1000u, "flushed epoch is the latest");
    ok(!mrd_nmea_flush(&n), "flushing twice does nothing");
}

/* ---------------------------------------------------------- robustness -- */

static void test_garbage(void) {
    printf("malformed input\n");

    static const char* const junk[] = {
        "",
        "$",
        "$G",
        "$GPGGA",
        "$GPGGA,",
        "*47",
        "$GPGGA*",
        "$GPGGA*4",
        "$GPGGA*ZZ",
        "not a sentence at all",
        "$GPGSV,0,0,00",
        "$GPGSV,2,3,08,01,40,083,46",
        "$GPGSV,1,1,99,,,,,,,,,,,,,,,",
        "$GPGGA,,,,,,,,,,,,,,",
        "$GPRMC,,,,,,,,,,",
        "$GPGSA,A,9,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99",
        "$GPGGA,999999,9999.9999,Z,99999.9999,Z,9,99,99.9,99999.9,M,,M,,",
        "$$$$$$$$$$",
        ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,",
    };

    MrdNmea n;
    mrd_nmea_init(&n);
    for(size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        mrd_nmea_feed(&n, junk[i]); /* must simply not crash or corrupt state */
    }
    ok(n.sat_count <= MRD_MAX_SATS, "satellite table stayed in bounds");

    /* A sentence far longer than NMEA permits must be refused outright rather
     * than overrun the split buffer. */
    char big[512];
    memset(big, 'A', sizeof(big) - 1);
    big[0] = '$';
    big[sizeof(big) - 1] = '\0';
    ok(mrd_nmea_feed(&n, big) == MrdUpdRejected, "oversized sentence rejected");

    /* More satellites than the table holds: extras are dropped, never written
     * past the end. */
    mrd_nmea_init(&n);
    for(uint8_t m = 0; m < 16; m++) {
        char line[128];
        snprintf(
            line,
            sizeof(line),
            "$GPGSV,16,%u,64,%02u,10,100,40,%02u,20,120,41,%02u,30,140,42,%02u,40,160,43",
            (unsigned)(m + 1),
            (unsigned)(m * 4 + 1),
            (unsigned)(m * 4 + 2),
            (unsigned)(m * 4 + 3),
            (unsigned)(m * 4 + 4));
        mrd_nmea_feed(&n, line);
    }
    ok(n.sat_count == MRD_MAX_SATS, "satellite table saturates at its limit");
}

int main(void) {
    printf("Meridian - NMEA parser\n\n");

    test_checksum();
    test_fields();
    test_gga_rmc();
    test_gsa_gsv();
    test_epochs();
    test_garbage();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
