#pragma once

/*
 * A GPS receiver made of arithmetic.
 *
 * This emits real NMEA sentences, with real checksums, into the same parser
 * the UART feeds. Nothing downstream can tell the difference, which is the
 * whole point: demo mode is not a separate code path with canned screens, it
 * is the actual detector being run against a synthetic sky. If the engine can
 * be fooled, it gets fooled here first, on the bench, in front of a test.
 *
 * The clean scenarios are modelled on what a consumer L1 receiver under open
 * sky actually reports - eleven satellites climbing and setting at their own
 * rates, carrier power rising with elevation, a fix that wanders a metre or so
 * a second because that is what noise does. The attack scenarios start from
 * exactly that and then break one thing at a time, so you can watch which
 * check notices.
 *
 * Free of any Flipper dependency, like the parser and the engine it feeds.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    /** Open sky, stationary. What "nothing is wrong" looks like. */
    MrdScenarioClean = 0,
    /** Open sky, driving at 13 m/s. Motion on its own must not trip anything. */
    MrdScenarioDrive,
    /** Meaconing/takeover: the fix is dragged to a false spot and held there. */
    MrdScenarioStatic,
    /** The subtle one: position walks away while Doppler still reads zero. */
    MrdScenarioCarry,
    /** Time-shifting attack. Position stays honest; the clock does not. */
    MrdScenarioTime,
    /** Lock is suppressed, then handed back four kilometres away. */
    MrdScenarioCapture,
    /** Denial, not deception. The noise floor comes up and the fix dies. */
    MrdScenarioJamming,

    MrdScenarioCount,
} MrdScenario;

/** Epoch at which an attack scenario stops behaving and starts attacking. */
#define MRD_SIM_TAKEOVER 8

typedef struct {
    uint8_t scenario;
    uint32_t rng;
    uint32_t epoch; /**< seconds of simulated time elapsed */
    uint32_t utc_ms; /**< the receiver's idea of the time */

    double lat, lon; /**< where the receiver currently believes it is */
    float alt;
    float sog; /**< speed over ground it reports, m/s */
    float cog;
    bool locked;
} MrdSim;

/** Emitted one complete sentence at a time, terminator excluded. */
typedef void (*MrdSimEmit)(void* context, const char* line);

void mrd_sim_init(MrdSim* s, MrdScenario scenario, uint32_t seed);

/** Advance one second and emit that second's sentence burst. */
void mrd_sim_step(MrdSim* s, MrdSimEmit emit, void* context);

/** Simulated milliseconds since the run started — this is the *local* clock a
 * detector should be given, and it is never affected by a time attack. */
uint32_t mrd_sim_mono_ms(const MrdSim* s);

const char* mrd_sim_name(MrdScenario scenario);
const char* mrd_sim_blurb(MrdScenario scenario);

/** True for the scenarios that are meant to be caught. */
bool mrd_sim_is_attack(MrdScenario scenario);
