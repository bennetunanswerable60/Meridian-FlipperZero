#include "mrd_gps.h"

#include <expansion/expansion.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>

#define RX_STREAM_SIZE 1024
#define WORKER_STACK 3072

struct MrdGps {
    FuriThread* thread;
    FuriStreamBuffer* rx;
    FuriHalSerialHandle* serial;
    Expansion* expansion;
    volatile bool running;

    bool demo;
    uint8_t scenario;
    uint32_t baud;
    uint8_t port;
    MrdSensitivity sens;
    uint16_t hold_s;

    /* Worker-owned. Nothing outside the thread may touch these. */
    MrdNmea nmea;
    MrdDetect det;
    MrdSim sim;

    FuriMutex* lock;
    MrdSnapshot snap;

    ViewDispatcher* dispatcher;
    uint32_t epoch_event;
};

/* ------------------------------------------------------------ the clock -- */

/** Local monotonic milliseconds. Deliberately not derived from the receiver:
 * it is the reference the receiver's own clock gets judged against, so an
 * attacker must not be able to move it. */
static uint32_t mono_ms(void) {
    uint32_t freq = furi_kernel_get_tick_frequency();
    if(freq == 1000) return furi_get_tick(); /* the usual case, no division */
    return (uint32_t)((uint64_t)furi_get_tick() * 1000u / freq);
}

/* ------------------------------------------------------------- publish -- */

static void publish(MrdGps* gps, bool link_up) {
    furi_mutex_acquire(gps->lock, FuriWaitForever);

    gps->snap.fix = gps->nmea.epoch;
    memcpy(gps->snap.sats, gps->nmea.sats, sizeof(gps->snap.sats));
    gps->snap.sat_count = gps->nmea.sat_count;
    gps->snap.det = gps->det;
    gps->snap.sentences = gps->nmea.sentences;
    gps->snap.bad_checksum = gps->nmea.bad_checksum;
    gps->snap.link_up = link_up;
    gps->snap.demo = gps->demo;
    gps->snap.scenario = gps->scenario;

    furi_mutex_release(gps->lock);
}

/** Record the most recent sentence so the wiring screen can show that bytes
 * really are arriving, and what they look like. */
static void note_line(MrdGps* gps, const char* line) {
    furi_mutex_acquire(gps->lock, FuriWaitForever);
    strncpy(gps->snap.last_line, line, MRD_LAST_LINE - 1);
    gps->snap.last_line[MRD_LAST_LINE - 1] = '\0';
    furi_mutex_release(gps->lock);
}

/** One sentence in; an epoch out, sometimes. */
static void consume(MrdGps* gps, const char* line, uint32_t now_ms) {
    uint8_t upd = mrd_nmea_feed(&gps->nmea, line);

    if(upd & MrdUpdEpoch) {
        mrd_detect_epoch(
            &gps->det, &gps->nmea.epoch, gps->nmea.sats, gps->nmea.sat_count, now_ms);

        furi_mutex_acquire(gps->lock, FuriWaitForever);
        gps->snap.epochs++;
        if(gps->nmea.epoch.valid) {
            mrd_trail_push(&gps->snap.trail, gps->nmea.epoch.lat, gps->nmea.epoch.lon);
        }
        furi_mutex_release(gps->lock);

        publish(gps, true);
        if(gps->dispatcher) {
            view_dispatcher_send_custom_event(gps->dispatcher, gps->epoch_event);
        }
    }
}

/* -------------------------------------------------------------- worker -- */

static void sim_emit(void* context, const char* line) {
    MrdGps* gps = context;
    note_line(gps, line);
    consume(gps, line, mrd_sim_mono_ms(&gps->sim));
}

static int32_t worker(void* context) {
    MrdGps* gps = context;

    char line[MRD_NMEA_LINE_MAX];
    size_t pos = 0;
    uint8_t buf[64];
    uint32_t last_rx = mono_ms();
    uint32_t next_sim = mono_ms();
    bool link_up = false;

    while(gps->running) {
        uint32_t now = mono_ms();

        if(gps->demo) {
            if((int32_t)(now - next_sim) >= 0) {
                next_sim = now + MRD_DEMO_STEP_MS;
                mrd_sim_step(&gps->sim, sim_emit, gps);
                if(!link_up) {
                    link_up = true;
                    publish(gps, true);
                }
            }
            furi_delay_ms(20);
            continue;
        }

        size_t got = furi_stream_buffer_receive(gps->rx, buf, sizeof(buf), 50);
        for(size_t i = 0; i < got; i++) {
            char c = (char)buf[i];
            if(c == '\n' || c == '\r') {
                if(pos > 0) {
                    line[pos] = '\0';
                    note_line(gps, line);
                    consume(gps, line, mono_ms());
                    pos = 0;
                }
            } else if(pos < sizeof(line) - 1) {
                line[pos++] = c;
            } else {
                /* A sentence longer than NMEA allows: drop it and resync on
                 * the next terminator rather than splicing two together. */
                pos = 0;
            }
        }

        if(got > 0) {
            last_rx = mono_ms();
            if(!link_up) {
                link_up = true;
                publish(gps, true);
            }
        } else if(link_up && (mono_ms() - last_rx) > MRD_LINK_TIMEOUT_MS) {
            /* The module has gone quiet. That is worth saying out loud - it is
             * far and away the most common wiring mistake. */
            link_up = false;
            publish(gps, false);
            if(gps->dispatcher) {
                view_dispatcher_send_custom_event(gps->dispatcher, gps->epoch_event);
            }
        }
    }
    return 0;
}

static void rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    MrdGps* gps = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(gps->rx, &data, 1, 0);
    }
}

/* ----------------------------------------------------------- lifecycle -- */

MrdGps* mrd_gps_alloc(ViewDispatcher* dispatcher, uint32_t epoch_event) {
    MrdGps* gps = malloc(sizeof(MrdGps));
    memset(gps, 0, sizeof(MrdGps));

    gps->dispatcher = dispatcher;
    gps->epoch_event = epoch_event;
    gps->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    gps->baud = 9600;
    gps->sens = MrdSensNormal;
    gps->hold_s = 60;

    mrd_nmea_init(&gps->nmea);
    mrd_detect_init(&gps->det, gps->sens, gps->hold_s);

    return gps;
}

void mrd_gps_free(MrdGps* gps) {
    furi_assert(gps);
    mrd_gps_stop(gps);
    furi_mutex_free(gps->lock);
    free(gps);
}

void mrd_gps_configure(MrdGps* gps, const MrdSettings* settings) {
    furi_assert(gps);
    gps->baud = mrd_baud_value(settings->baud);
    gps->port = settings->port;
    gps->sens = (MrdSensitivity)settings->sensitivity;
    gps->hold_s = settings->hold_s;

    /* Sensitivity is safe to change mid-session; it only moves thresholds. */
    mrd_detect_set_sensitivity(&gps->det, gps->sens);
}

void mrd_gps_reset(MrdGps* gps) {
    furi_assert(gps);
    furi_check(!gps->running); /* the worker owns these while it is up */

    mrd_nmea_init(&gps->nmea);
    mrd_detect_init(&gps->det, gps->sens, gps->hold_s);

    furi_mutex_acquire(gps->lock, FuriWaitForever);
    memset(&gps->snap, 0, sizeof(gps->snap));
    gps->snap.det = gps->det;
    gps->snap.fix.utc_ms = MRD_UTC_UNKNOWN;
    furi_mutex_release(gps->lock);
}

static void start_worker(MrdGps* gps) {
    gps->running = true;
    gps->thread = furi_thread_alloc_ex("MeridianGps", WORKER_STACK, worker, gps);
    furi_thread_start(gps->thread);
}

void mrd_gps_start_live(MrdGps* gps) {
    furi_assert(gps);
    if(gps->running) return;

    mrd_gps_reset(gps);
    gps->demo = false;

    /* The Expansion service holds the USART open looking for add-on boards.
     * Take the pins for the duration. */
    gps->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(gps->expansion);

    gps->rx = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    start_worker(gps);

    FuriHalSerialId id = (gps->port == MrdPortLpuart) ? FuriHalSerialIdLpuart :
                                                        FuriHalSerialIdUsart;
    gps->serial = furi_hal_serial_control_acquire(id);
    furi_check(gps->serial);
    furi_hal_serial_init(gps->serial, gps->baud);
    furi_hal_serial_async_rx_start(gps->serial, rx_isr, gps, false);
}

void mrd_gps_start_demo(MrdGps* gps, MrdScenario scenario) {
    furi_assert(gps);
    if(gps->running) return;

    mrd_gps_reset(gps);
    gps->demo = true;
    gps->scenario = (uint8_t)scenario;
    mrd_sim_init(&gps->sim, scenario, furi_get_tick() | 1u);

    furi_mutex_acquire(gps->lock, FuriWaitForever);
    gps->snap.demo = true;
    gps->snap.scenario = (uint8_t)scenario;
    furi_mutex_release(gps->lock);

    start_worker(gps);
}

void mrd_gps_stop(MrdGps* gps) {
    furi_assert(gps);
    if(!gps->running) return;

    /* The serial goes down first so no more bytes can arrive while the worker
     * is winding up. */
    if(gps->serial) {
        furi_hal_serial_async_rx_stop(gps->serial);
        furi_hal_serial_deinit(gps->serial);
        furi_hal_serial_control_release(gps->serial);
        gps->serial = NULL;
    }

    gps->running = false;
    if(gps->thread) {
        furi_thread_join(gps->thread);
        furi_thread_free(gps->thread);
        gps->thread = NULL;
    }

    if(gps->rx) {
        furi_stream_buffer_free(gps->rx);
        gps->rx = NULL;
    }

    if(gps->expansion) {
        expansion_enable(gps->expansion);
        furi_record_close(RECORD_EXPANSION);
        gps->expansion = NULL;
    }

    /* Whatever second was mid-assembly is still a second; publish it rather
     * than throwing it away. */
    if(mrd_nmea_flush(&gps->nmea)) publish(gps, false);
}

bool mrd_gps_is_running(MrdGps* gps) {
    return gps->running;
}

void mrd_gps_snapshot(MrdGps* gps, MrdSnapshot* out) {
    furi_assert(gps);
    furi_mutex_acquire(gps->lock, FuriWaitForever);
    *out = gps->snap;
    furi_mutex_release(gps->lock);
}
