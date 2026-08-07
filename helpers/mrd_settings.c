#include "mrd_settings.h"

#include <furi.h>
#include <storage/storage.h>

#define SETTINGS_MAGIC "meridian.1"

static const uint32_t BAUDS[MrdBaudCount] = {9600, 38400, 57600, 115200};

uint32_t mrd_baud_value(uint8_t id) {
    return BAUDS[id < MrdBaudCount ? id : 0];
}

const char* mrd_baud_name(uint8_t id) {
    switch(id) {
    case MrdBaud9600:
        return "9600";
    case MrdBaud38400:
        return "38400";
    case MrdBaud57600:
        return "57600";
    case MrdBaud115200:
        return "115200";
    default:
        return "?";
    }
}

const char* mrd_port_name(uint8_t id) {
    return (id == MrdPortLpuart) ? "LPUART" : "USART";
}

const char* mrd_port_pins(uint8_t id) {
    return (id == MrdPortLpuart) ? "15 TX / 16 RX" : "13 TX / 14 RX";
}

const char* mrd_sensitivity_name(uint8_t id) {
    switch(id) {
    case MrdSensLow:
        return "Low";
    case MrdSensHigh:
        return "High";
    default:
        return "Normal";
    }
}

void mrd_settings_default(MrdSettings* s) {
    memset(s, 0, sizeof(*s));
    s->baud = MrdBaud9600;
    s->port = MrdPortUsart;
    s->sensitivity = MrdSensNormal;
    s->hold_s = 60;
    s->sound = true;
    s->led = true;
    s->logging = false;
}

void mrd_settings_load(MrdSettings* s) {
    mrd_settings_default(s);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, MRD_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[128];
        memset(buf, 0, sizeof(buf));
        uint16_t got = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[got] = '\0';

        unsigned baud = 0, port = 0, sens = 0, hold = 0, snd = 0, led = 0, log = 0;
        char magic[16] = {0};
        /* Version-tagged and read as a whole: a partial match is treated as no
         * match, so a settings file from a future build cannot half-apply. */
        if(sscanf(
               buf,
               "%15s %u %u %u %u %u %u %u",
               magic,
               &baud,
               &port,
               &sens,
               &hold,
               &snd,
               &led,
               &log) == 8 &&
           strcmp(magic, SETTINGS_MAGIC) == 0) {
            if(baud < MrdBaudCount) s->baud = (uint8_t)baud;
            if(port < MrdPortCount) s->port = (uint8_t)port;
            if(sens <= MrdSensHigh) s->sensitivity = (uint8_t)sens;
            if(hold >= 10 && hold <= 600) s->hold_s = (uint16_t)hold;
            s->sound = snd != 0;
            s->led = led != 0;
            s->logging = log != 0;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void mrd_settings_save(const MrdSettings* s) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, MRD_LOG_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, MRD_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[128];
        int n = snprintf(
            buf,
            sizeof(buf),
            "%s %u %u %u %u %u %u %u\n",
            SETTINGS_MAGIC,
            (unsigned)s->baud,
            (unsigned)s->port,
            (unsigned)s->sensitivity,
            (unsigned)s->hold_s,
            (unsigned)(s->sound ? 1 : 0),
            (unsigned)(s->led ? 1 : 0),
            (unsigned)(s->logging ? 1 : 0));
        if(n > 0) storage_file_write(file, buf, (uint16_t)n);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
