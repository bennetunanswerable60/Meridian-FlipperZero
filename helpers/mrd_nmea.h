#pragma once

/*
 * NMEA 0183 parser.
 *
 * Deliberately free of any Flipper dependency: this file and mrd_nmea.c
 * compile on the host, so the parser is tested against real receiver output
 * in test/host_nmea_test.c rather than being trusted because it looked right.
 *
 * Everything the integrity engine needs comes out of four sentence types:
 *
 *   GGA  position, fix quality, satellites used, HDOP, altitude
 *   RMC  position, validity, speed and course over ground, UTC date
 *   GSA  fix dimension, the PRNs actually used, PDOP/HDOP/VDOP
 *   GSV  every satellite in view, with elevation, azimuth and C/N0
 *
 * GSV is the interesting one. It is the only sentence that describes the sky
 * rather than the solution, and almost every statistical tell a single-antenna
 * receiver can offer about spoofing lives in those three numbers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Satellites tracked at once. Multi-GNSS receivers routinely report 30+ in
 * view across four constellations; anything past this is dropped, which only
 * costs us sky-plot detail and never affects a fix. */
#define MRD_MAX_SATS 32

/** Longest sentence we will look at. NMEA 0183 caps a sentence at 82 bytes,
 * but plenty of receivers exceed it on GSV, so there is room to spare. */
#define MRD_NMEA_LINE_MAX 128

/** Sentinel for "this receiver has not given us a time yet". */
#define MRD_UTC_UNKNOWN UINT32_MAX

/*
 * The firmware compiles with -fsingle-precision-constant, which makes an
 * unsuffixed floating literal a float, and -Wdouble-promotion, which rejects
 * letting one meet a double. So every constant that touches a coordinate has to
 * say out loud that it is a double.
 *
 * Coordinates are the one thing in this application that genuinely need the
 * extra precision. A float carries a 24-bit mantissa, which quantises latitude
 * to roughly 0.3 m at mid latitudes - enough to visibly grid the drift plot,
 * and enough to make the frozen-position check declare two fixes identical when
 * the receiver really did move between them. That check compares coordinates
 * for exact equality, so its correctness rests directly on this.
 *
 * The constants themselves only carry float precision, and that is fine: they
 * are scale factors applied to differences that were computed in double, so
 * their error is a systematic 2e-8 on a distance rather than noise on a
 * position.
 */
#define MRD_D(x) ((double)(x))

typedef enum {
    MrdSysUnknown = 0,
    MrdSysGps,
    MrdSysGlonass,
    MrdSysGalileo,
    MrdSysBeidou,
    MrdSysCount,
} MrdSystem;

/** One satellite, as the receiver reports it. */
typedef struct {
    uint8_t prn; /**< satellite id within its constellation */
    uint8_t sys; /**< MrdSystem, from the talker id */
    uint8_t elev; /**< degrees above the horizon, 0..90 */
    uint16_t azim; /**< degrees clockwise from true north, 0..359 */
    uint8_t snr; /**< carrier-to-noise density, dB-Hz; 0 = in view, not tracked */
    bool used; /**< named in GSA, i.e. contributing to the fix */
} MrdSat;

/** The navigation solution for one epoch. */
typedef struct {
    bool valid; /**< RMC status A, or GGA quality > 0 */
    double lat; /**< degrees, + north */
    double lon; /**< degrees, + east */
    float alt_m; /**< metres above mean sea level */
    float hdop;
    float pdop;
    float vdop;
    float speed_mps; /**< speed over ground, from RMC/VTG (Doppler derived) */
    float course_deg;
    uint8_t quality; /**< GGA fix quality: 0 none, 1 GPS, 2 DGPS, 6 dead reckoning... */
    uint8_t fix_type; /**< GSA: 1 no fix, 2 = 2D, 3 = 3D */
    uint8_t sats_used;
    uint8_t sats_view;
    uint32_t utc_ms; /**< ms since midnight UTC, or MRD_UTC_UNKNOWN */
    uint32_t date; /**< ddmmyy as an integer, 0 if unknown */
    bool has_alt;
    bool has_speed;
} MrdFix;

/** What a fed sentence changed. Bit flags, so one feed can report several. */
typedef enum {
    MrdUpdNone = 0,
    MrdUpdPosition = 1 << 0, /**< GGA/RMC/GLL carried a position */
    MrdUpdSats = 1 << 1, /**< GSV/GSA changed the satellite table */
    MrdUpdEpoch = 1 << 2, /**< the previous second is complete and consistent */
    MrdUpdRejected = 1 << 3, /**< checksum failed, or not a sentence at all */
} MrdNmeaUpdate;

/** Parser state. Plain struct so it can live inside the app with no allocator. */
typedef struct {
    MrdFix fix; /**< accumulating: the epoch currently being built */
    MrdFix epoch; /**< the last complete epoch, safe to read */
    MrdSat sats[MRD_MAX_SATS];
    uint8_t sat_count;

    /* GSV assembly, per constellation, because each talker runs its own
     * message series and they interleave on multi-GNSS receivers. */
    uint8_t gsv_seen[MrdSysCount]; /**< sats filled in this series so far */
    uint8_t gsv_total[MrdSysCount]; /**< sats the series promised */

    uint32_t epoch_utc; /**< UTC of the epoch being assembled */
    bool epoch_open;

    /* Link health. Counted rather than acted on: a receiver that is wired up
     * wrong and one that is being interfered with look identical here, so the
     * numbers are shown to the user instead of being scored. */
    uint32_t sentences; /**< accepted */
    uint32_t bad_checksum;
    uint32_t unknown; /**< well-formed but not a type we use */
} MrdNmea;

void mrd_nmea_init(MrdNmea* n);

/**
 * Feed one sentence with the line terminator already stripped.
 *
 * Returns a bitwise-or of MrdNmeaUpdate. When MrdUpdEpoch comes back, @c
 * n->epoch and the satellite table together describe one complete second and
 * are ready to hand to the detector.
 */
uint8_t mrd_nmea_feed(MrdNmea* n, const char* line);

/**
 * Close the epoch under assembly without waiting for the next second's first
 * sentence. Used when the stream stops, so the last second is not lost.
 * Returns true if there was anything to close.
 */
bool mrd_nmea_flush(MrdNmea* n);

/** NMEA checksum: XOR of every byte between '$' and '*'. */
uint8_t mrd_nmea_checksum(const char* body, size_t len);

/** Short constellation label, e.g. "GPS", "GLO". Never NULL. */
const char* mrd_system_name(uint8_t sys);
const char* mrd_system_short(uint8_t sys);

/* ---- exposed for the tests, and used by the detector ---- */

/** Parse a signed decimal like "-12.345" without pulling in strtod. Returns
 * false if @p s is empty or malformed. */
bool mrd_parse_double(const char* s, double* out);

/** ddmm.mmmm / dddmm.mmmm plus a hemisphere character, to signed degrees. */
bool mrd_parse_latlon(const char* s, char hemi, double* deg);

/** hhmmss[.sss] to milliseconds since midnight UTC. */
bool mrd_parse_time(const char* s, uint32_t* ms);
