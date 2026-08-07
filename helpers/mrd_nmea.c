#include "mrd_nmea.h"

#include <string.h>

/* Fields in one sentence. GSV with four satellites is the widest thing we
 * parse at 20; NMEA 4.11 GSA is 19. */
#define MRD_MAX_FIELDS 26

typedef struct {
    char buf[MRD_NMEA_LINE_MAX];
    const char* f[MRD_MAX_FIELDS];
    uint8_t n;
} MrdSplit;

/* ------------------------------------------------------------- numbers -- */

/*
 * Hand-rolled rather than strtod. Two reasons, and the second is the one that
 * matters: it keeps the parser free of locale machinery, and it guarantees
 * that identical input text produces a bit-identical double. The frozen-
 * position check tests consecutive fixes for exact equality, so "does the same
 * text always parse to the same bits" is load-bearing, not a nicety.
 */
bool mrd_parse_double(const char* s, double* out) {
    if(!s || !*s) return false;

    /* Guard on the accumulators rather than the digit count, so a pathological
     * field is clamped instead of wrapping. */
    const uint64_t LIMIT = 1000000000000000000ull;

    const char* p = s;
    bool neg = false;
    if(*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }

    bool any = false;
    uint64_t ip = 0;
    while(*p >= '0' && *p <= '9') {
        if(ip < LIMIT) ip = ip * 10u + (uint64_t)(*p - '0');
        p++;
        any = true;
    }

    uint64_t frac = 0, scale = 1;
    if(*p == '.') {
        p++;
        while(*p >= '0' && *p <= '9') {
            /* Accumulated as integers and divided once, so the result is the
             * correctly-rounded value rather than a pile of per-digit rounding
             * errors - and identical text always lands on identical bits,
             * which the frozen-position check depends on. */
            if(scale < LIMIT) {
                frac = frac * 10u + (uint64_t)(*p - '0');
                scale *= 10u;
            }
            p++;
            any = true;
        }
    }

    if(!any || *p != '\0') return false; /* empty, or trailing junk */

    double v = (double)ip;
    if(scale > 1) v += (double)frac / (double)scale;
    *out = neg ? -v : v;
    return true;
}

static bool parse_uint(const char* s, uint32_t* out) {
    if(!s || !*s) return false;
    uint32_t v = 0;
    for(const char* p = s; *p; p++) {
        if(*p < '0' || *p > '9') return false;
        if(v > 429496728u) return false;
        v = v * 10u + (uint32_t)(*p - '0');
    }
    *out = v;
    return true;
}

bool mrd_parse_latlon(const char* s, char hemi, double* deg) {
    if(!s || !*s) return false;

    /* ddmm.mmmm: the two digits immediately left of the point are minutes,
     * everything left of those is degrees. Works for both 2- and 3-digit
     * degree fields without a separate longitude path. */
    const char* dot = strchr(s, '.');
    size_t intlen = dot ? (size_t)(dot - s) : strlen(s);
    if(intlen < 3 || intlen > 5) return false;

    char dbuf[4];
    size_t dlen = intlen - 2;
    if(dlen >= sizeof(dbuf)) return false;
    memcpy(dbuf, s, dlen);
    dbuf[dlen] = '\0';

    double d, m;
    if(!mrd_parse_double(dbuf, &d)) return false;
    if(!mrd_parse_double(s + dlen, &m)) return false;
    if(m < MRD_D(0) || m >= MRD_D(60)) return false;

    double v = d + m / MRD_D(60);
    if(hemi == 'S' || hemi == 'W') {
        v = -v;
    } else if(hemi != 'N' && hemi != 'E') {
        return false;
    }

    *deg = v;
    return true;
}

bool mrd_parse_time(const char* s, uint32_t* ms) {
    if(!s) return false;
    size_t n = strlen(s);
    if(n < 6) return false;

    for(int i = 0; i < 6; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
    }

    uint32_t hh = (uint32_t)(s[0] - '0') * 10u + (uint32_t)(s[1] - '0');
    uint32_t mm = (uint32_t)(s[2] - '0') * 10u + (uint32_t)(s[3] - '0');
    uint32_t ss = (uint32_t)(s[4] - '0') * 10u + (uint32_t)(s[5] - '0');
    if(hh > 23 || mm > 59 || ss > 60) return false; /* 60 is a leap second */

    uint32_t frac = 0;
    if(n > 6) {
        if(s[6] != '.') return false;
        double f;
        if(mrd_parse_double(s + 6, &f)) frac = (uint32_t)(f * MRD_D(1000) + MRD_D(0.5));
        if(frac > 999) frac = 999;
    }

    *ms = (hh * 3600u + mm * 60u + ss) * 1000u + frac;
    return true;
}

/* ---------------------------------------------------------- talker ids -- */

static uint8_t talker_to_system(const char* t) {
    if(t[0] != 'G' && !(t[0] == 'B' && t[1] == 'D')) return MrdSysUnknown;
    switch(t[1]) {
    case 'P':
        return MrdSysGps;
    case 'L':
        return MrdSysGlonass;
    case 'A':
        return MrdSysGalileo;
    case 'B':
        return MrdSysBeidou;
    case 'D':
        return MrdSysBeidou; /* BD, older BeiDou receivers */
    default:
        return MrdSysUnknown; /* GN combined, GQ QZSS, GI NavIC */
    }
}

/*
 * NMEA 4.10 unified satellite numbering. A combined-talker sentence ($GNGSA,
 * $GNGSV) has no other way to say which constellation a satellite belongs to,
 * so the number itself carries it. Single-constellation talkers mostly use
 * the same ranges, and where they do not (Galileo and BeiDou both start at 1
 * on their own talker) the talker fills the gap.
 */
static void decode_prn(uint8_t talker_sys, uint32_t raw, uint8_t* sys, uint8_t* prn) {
    if(raw >= 301 && raw <= 336) {
        *sys = MrdSysGalileo;
        *prn = (uint8_t)(raw - 300);
    } else if(raw >= 201 && raw <= 237) {
        *sys = MrdSysBeidou;
        *prn = (uint8_t)(raw - 200);
    } else if(raw >= 193 && raw <= 199) {
        *sys = MrdSysUnknown; /* QZSS */
        *prn = (uint8_t)raw;
    } else if(raw >= 65 && raw <= 96) {
        *sys = MrdSysGlonass;
        *prn = (uint8_t)(raw - 64);
    } else if(raw >= 33 && raw <= 64) {
        *sys = MrdSysUnknown; /* SBAS */
        *prn = (uint8_t)raw;
    } else {
        *sys = (talker_sys != MrdSysUnknown) ? talker_sys : MrdSysGps;
        *prn = (uint8_t)raw;
    }
}

const char* mrd_system_name(uint8_t sys) {
    switch(sys) {
    case MrdSysGps:
        return "GPS";
    case MrdSysGlonass:
        return "GLONASS";
    case MrdSysGalileo:
        return "Galileo";
    case MrdSysBeidou:
        return "BeiDou";
    default:
        return "Other";
    }
}

const char* mrd_system_short(uint8_t sys) {
    switch(sys) {
    case MrdSysGps:
        return "GPS";
    case MrdSysGlonass:
        return "GLO";
    case MrdSysGalileo:
        return "GAL";
    case MrdSysBeidou:
        return "BDS";
    default:
        return "SBS";
    }
}

/* ------------------------------------------------------------ checksum -- */

uint8_t mrd_nmea_checksum(const char* body, size_t len) {
    uint8_t cs = 0;
    for(size_t i = 0; i < len; i++) cs ^= (uint8_t)body[i];
    return cs;
}

static bool hex2(const char* s, uint8_t* out) {
    uint8_t v = 0;
    for(int i = 0; i < 2; i++) {
        char c = s[i];
        uint8_t d;
        if(c >= '0' && c <= '9') {
            d = (uint8_t)(c - '0');
        } else if(c >= 'A' && c <= 'F') {
            d = (uint8_t)(c - 'A' + 10);
        } else if(c >= 'a' && c <= 'f') {
            d = (uint8_t)(c - 'a' + 10);
        } else {
            return false;
        }
        v = (uint8_t)((v << 4) | d);
    }
    *out = v;
    return true;
}

/* --------------------------------------------------------------- split -- */

/** Verify the checksum if present, then split the body on commas. */
static bool split_sentence(const char* line, MrdSplit* sp, bool* checksum_bad) {
    *checksum_bad = false;

    if(!line || (line[0] != '$' && line[0] != '!')) return false;

    size_t len = strlen(line);
    if(len < 7 || len >= MRD_NMEA_LINE_MAX) return false;

    /* Body runs from after '$' up to '*', if there is one. */
    const char* star = strchr(line, '*');
    size_t body_len = star ? (size_t)(star - line) - 1 : len - 1;
    if(body_len == 0 || body_len >= MRD_NMEA_LINE_MAX) return false;

    if(star) {
        /* A sentence that carries a checksum has to pass it. A sentence that
         * carries none is accepted: some receivers and most GPS simulators
         * omit it, and refusing those would make the app useless on hardware
         * that is otherwise fine. */
        uint8_t want;
        if(strlen(star) < 3 || !hex2(star + 1, &want)) return false;
        if(mrd_nmea_checksum(line + 1, body_len) != want) {
            *checksum_bad = true;
            return false;
        }
    }

    memcpy(sp->buf, line + 1, body_len);
    sp->buf[body_len] = '\0';

    sp->n = 0;
    sp->f[sp->n++] = sp->buf;
    for(char* p = sp->buf; *p; p++) {
        if(*p == ',') {
            *p = '\0';
            if(sp->n < MRD_MAX_FIELDS) sp->f[sp->n++] = p + 1;
        }
    }
    return true;
}

/** Field @p i, or "" when the sentence is short or the field is empty. */
static const char* fld(const MrdSplit* sp, uint8_t i) {
    return (i < sp->n) ? sp->f[i] : "";
}

/* ----------------------------------------------------------- sat table -- */

static MrdSat* sat_slot(MrdNmea* n, uint8_t sys, uint8_t prn) {
    for(uint8_t i = 0; i < n->sat_count; i++) {
        if(n->sats[i].prn == prn && n->sats[i].sys == sys) return &n->sats[i];
    }
    if(n->sat_count >= MRD_MAX_SATS) return NULL;
    MrdSat* s = &n->sats[n->sat_count++];
    memset(s, 0, sizeof(*s));
    s->prn = prn;
    s->sys = sys;
    return s;
}

/** Drop every satellite of one constellation, ahead of a fresh GSV series. */
static void sat_clear_system(MrdNmea* n, uint8_t sys) {
    uint8_t w = 0;
    for(uint8_t i = 0; i < n->sat_count; i++) {
        if(n->sats[i].sys != sys) n->sats[w++] = n->sats[i];
    }
    n->sat_count = w;
}

static void sat_clear_used(MrdNmea* n) {
    for(uint8_t i = 0; i < n->sat_count; i++) n->sats[i].used = false;
}

/* -------------------------------------------------------------- epochs -- */

static void epoch_reset(MrdNmea* n, uint32_t utc) {
    memset(&n->fix, 0, sizeof(n->fix));
    n->fix.utc_ms = utc;
    n->fix.hdop = 0.0f;
    n->epoch_utc = utc;
    n->epoch_open = true;
}

static void epoch_publish(MrdNmea* n) {
    uint16_t view = 0;
    for(uint8_t i = 0; i < MrdSysCount; i++) view = (uint16_t)(view + n->gsv_total[i]);
    if(view == 0) view = n->sat_count;
    if(view > 255) view = 255;

    n->fix.sats_view = (uint8_t)view;
    n->epoch = n->fix;
    n->epoch_open = false;
}

bool mrd_nmea_flush(MrdNmea* n) {
    if(!n->epoch_open) return false;
    epoch_publish(n);
    return true;
}

/**
 * Time-driven epoch boundary: a receiver emits its whole sentence burst under
 * one timestamp, so the first sentence carrying a *different* time is the
 * signal that the previous second is complete. Returns true if that just
 * happened, meaning n->epoch is newly valid.
 */
static bool epoch_advance(MrdNmea* n, uint32_t utc) {
    if(utc == MRD_UTC_UNKNOWN) return false;

    if(!n->epoch_open) {
        epoch_reset(n, utc);
        return false;
    }
    if(utc == n->epoch_utc) return false;

    epoch_publish(n);
    epoch_reset(n, utc);
    return true;
}

/* ------------------------------------------------------------ sentences -- */

static void parse_gga(MrdNmea* n, const MrdSplit* sp) {
    double lat, lon;
    if(mrd_parse_latlon(fld(sp, 2), fld(sp, 3)[0], &lat) &&
       mrd_parse_latlon(fld(sp, 4), fld(sp, 5)[0], &lon) && lat >= MRD_D(-90) && lat <= MRD_D(90) &&
       lon >= MRD_D(-180) && lon <= MRD_D(180)) {
        n->fix.lat = lat;
        n->fix.lon = lon;
    }

    uint32_t q;
    if(parse_uint(fld(sp, 6), &q) && q <= 8) {
        n->fix.quality = (uint8_t)q;
        if(q > 0) n->fix.valid = true;
    }

    uint32_t used;
    if(parse_uint(fld(sp, 7), &used) && used <= 64) n->fix.sats_used = (uint8_t)used;

    double h;
    if(mrd_parse_double(fld(sp, 8), &h) && h > MRD_D(0) && h < MRD_D(100)) {
        n->fix.hdop = (float)h;
    }

    double alt;
    if(mrd_parse_double(fld(sp, 9), &alt)) {
        n->fix.alt_m = (float)alt;
        n->fix.has_alt = true;
    }
}

static void parse_rmc(MrdNmea* n, const MrdSplit* sp) {
    if(fld(sp, 2)[0] == 'A') n->fix.valid = true;

    double lat, lon;
    if(mrd_parse_latlon(fld(sp, 3), fld(sp, 4)[0], &lat) &&
       mrd_parse_latlon(fld(sp, 5), fld(sp, 6)[0], &lon) && lat >= MRD_D(-90) && lat <= MRD_D(90) &&
       lon >= MRD_D(-180) && lon <= MRD_D(180)) {
        n->fix.lat = lat;
        n->fix.lon = lon;
    }

    double sog;
    if(mrd_parse_double(fld(sp, 7), &sog) && sog >= MRD_D(0)) {
        n->fix.speed_mps = (float)sog * 0.514444f; /* knots to m/s */
        n->fix.has_speed = true;
    }

    double cog;
    if(mrd_parse_double(fld(sp, 8), &cog) && cog >= MRD_D(0) && cog < MRD_D(360)) {
        n->fix.course_deg = (float)cog;
    }

    uint32_t date;
    if(parse_uint(fld(sp, 9), &date) && date > 0) n->fix.date = date;
}

static void parse_gsa(MrdNmea* n, const MrdSplit* sp, uint8_t talker_sys) {
    uint32_t mode;
    if(parse_uint(fld(sp, 2), &mode) && mode >= 1 && mode <= 3) {
        /* Highest dimension wins across a multi-constellation burst: with
         * $GNGSA the per-system sentences disagree routinely, and the fix as
         * a whole is 3D if any of them solved in 3D. */
        if(mode > n->fix.fix_type) n->fix.fix_type = (uint8_t)mode;
    }

    for(uint8_t i = 3; i < 15; i++) {
        uint32_t raw;
        if(!parse_uint(fld(sp, i), &raw) || raw == 0) continue;
        uint8_t sys, prn;
        decode_prn(talker_sys, raw, &sys, &prn);
        MrdSat* s = sat_slot(n, sys, prn);
        if(s) s->used = true;
    }

    double v;
    if(mrd_parse_double(fld(sp, 15), &v) && v > MRD_D(0) && v < MRD_D(100)) n->fix.pdop = (float)v;
    if(mrd_parse_double(fld(sp, 16), &v) && v > MRD_D(0) && v < MRD_D(100)) n->fix.hdop = (float)v;
    if(mrd_parse_double(fld(sp, 17), &v) && v > MRD_D(0) && v < MRD_D(100)) n->fix.vdop = (float)v;
}

static void parse_gsv(MrdNmea* n, const MrdSplit* sp, uint8_t talker_sys) {
    uint32_t total_msgs, msg_num, in_view;
    if(!parse_uint(fld(sp, 1), &total_msgs) || !parse_uint(fld(sp, 2), &msg_num) ||
       !parse_uint(fld(sp, 3), &in_view)) {
        return;
    }
    if(msg_num == 0 || msg_num > total_msgs || in_view > 64) return;

    uint8_t series_sys = (talker_sys != MrdSysUnknown) ? talker_sys : MrdSysGps;

    /* Message 1 restarts the series: everything this constellation reported
     * last time is stale, including satellites it has now lost. */
    if(msg_num == 1) {
        sat_clear_system(n, series_sys);
        n->gsv_seen[series_sys] = 0;
        n->gsv_total[series_sys] = (uint8_t)in_view;
    }

    for(uint8_t blk = 0; blk < 4; blk++) {
        uint8_t base = (uint8_t)(4 + blk * 4);
        uint32_t raw;
        if(!parse_uint(fld(sp, base), &raw) || raw == 0) continue;

        uint8_t sys, prn;
        decode_prn(series_sys, raw, &sys, &prn);
        MrdSat* s = sat_slot(n, sys, prn);
        if(!s) continue;

        uint32_t v;
        if(parse_uint(fld(sp, (uint8_t)(base + 1)), &v) && v <= 90) s->elev = (uint8_t)v;
        if(parse_uint(fld(sp, (uint8_t)(base + 2)), &v) && v < 360) s->azim = (uint16_t)v;
        /* An empty C/N0 field means "in view, not tracked", which is a real
         * and different state from "tracked at 0 dB-Hz". Both land on 0 here;
         * the detector only ever looks at satellites with snr > 0. */
        s->snr = (parse_uint(fld(sp, (uint8_t)(base + 3)), &v) && v <= 99) ? (uint8_t)v : 0;

        if(n->gsv_seen[series_sys] < 255) n->gsv_seen[series_sys]++;
    }
}

/* ---------------------------------------------------------------- feed -- */

void mrd_nmea_init(MrdNmea* n) {
    memset(n, 0, sizeof(*n));
    n->fix.utc_ms = MRD_UTC_UNKNOWN;
    n->epoch.utc_ms = MRD_UTC_UNKNOWN;
}

uint8_t mrd_nmea_feed(MrdNmea* n, const char* line) {
    MrdSplit sp;
    bool cs_bad = false;

    if(!split_sentence(line, &sp, &cs_bad)) {
        if(cs_bad) n->bad_checksum++;
        return MrdUpdRejected;
    }

    const char* tag = sp.f[0];
    if(strlen(tag) < 5) {
        n->unknown++;
        return MrdUpdRejected;
    }

    uint8_t talker_sys = talker_to_system(tag);
    const char* type = tag + 2;
    uint8_t out = MrdUpdNone;

    /* Position sentences carry the timestamp, so they are what closes an
     * epoch. GSA and GSV do not, and simply accrue into whichever epoch is
     * open. */
    if(!strcmp(type, "GGA") || !strcmp(type, "RMC") || !strcmp(type, "GLL")) {
        uint8_t tf = (!strcmp(type, "GLL")) ? 5 : 1;
        uint32_t utc;
        if(mrd_parse_time(fld(&sp, tf), &utc)) {
            if(epoch_advance(n, utc)) out |= MrdUpdEpoch;
            /* GSA repopulates the used flags every second; clear them as the
             * new epoch opens so a satellite dropped from the solution does
             * not stay marked forever. */
            if(out & MrdUpdEpoch) sat_clear_used(n);
        } else if(!n->epoch_open) {
            epoch_reset(n, MRD_UTC_UNKNOWN);
        }
    } else if(!n->epoch_open) {
        epoch_reset(n, MRD_UTC_UNKNOWN);
    }

    if(!strcmp(type, "GGA")) {
        parse_gga(n, &sp);
        out |= MrdUpdPosition;
    } else if(!strcmp(type, "RMC")) {
        parse_rmc(n, &sp);
        out |= MrdUpdPosition;
    } else if(!strcmp(type, "GSA")) {
        parse_gsa(n, &sp, talker_sys);
        out |= MrdUpdSats;
    } else if(!strcmp(type, "GSV")) {
        parse_gsv(n, &sp, talker_sys);
        out |= MrdUpdSats;
    } else if(!strcmp(type, "VTG")) {
        double sog;
        if(mrd_parse_double(fld(&sp, 7), &sog) && sog >= MRD_D(0) && !n->fix.has_speed) {
            n->fix.speed_mps = (float)sog / 3.6f; /* km/h to m/s */
            n->fix.has_speed = true;
        }
    } else if(!strcmp(type, "GLL")) {
        /* Position-only sentence. Some receivers emit GLL and no GGA at all,
         * so it has to carry a fix rather than just a status flag. */
        double lat, lon;
        if(mrd_parse_latlon(fld(&sp, 1), fld(&sp, 2)[0], &lat) &&
           mrd_parse_latlon(fld(&sp, 3), fld(&sp, 4)[0], &lon) && lat >= MRD_D(-90) &&
           lat <= MRD_D(90) && lon >= MRD_D(-180) && lon <= MRD_D(180)) {
            n->fix.lat = lat;
            n->fix.lon = lon;
            out |= MrdUpdPosition;
        }
        if(fld(&sp, 6)[0] == 'A') n->fix.valid = true;
    } else {
        n->unknown++;
    }

    n->sentences++;
    return out;
}
