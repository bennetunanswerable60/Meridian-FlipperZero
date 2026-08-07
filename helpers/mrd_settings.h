#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mrd_detect.h"

#define MRD_SETTINGS_PATH "/ext/apps_data/meridian/settings.txt"
#define MRD_LOG_DIR "/ext/apps_data/meridian"

/** Baud rates a consumer NMEA module is likely to be running at. */
typedef enum {
    MrdBaud9600 = 0, /**< NEO-6M and most cheap modules, out of the box */
    MrdBaud38400, /**< NEO-M8 family default */
    MrdBaud57600,
    MrdBaud115200,
    MrdBaudCount,
} MrdBaudId;

/** Which of the Flipper's two serial ports the module is wired to. */
typedef enum {
    MrdPortUsart = 0, /**< GPIO 13 TX / 14 RX - the usual choice */
    MrdPortLpuart, /**< GPIO 15 TX / 16 RX - free if 13/14 are taken */
    MrdPortCount,
} MrdPortId;

typedef struct {
    uint8_t baud; /**< MrdBaudId */
    uint8_t port; /**< MrdPortId */
    uint8_t sensitivity; /**< MrdSensitivity */
    uint16_t hold_s; /**< how long a flag stays up after it fires */
    bool sound;
    bool led;
    bool logging; /**< write a CSV of every epoch to the SD card */
} MrdSettings;

void mrd_settings_default(MrdSettings* s);
void mrd_settings_load(MrdSettings* s);
void mrd_settings_save(const MrdSettings* s);

uint32_t mrd_baud_value(uint8_t id);
const char* mrd_baud_name(uint8_t id);
const char* mrd_port_name(uint8_t id);
const char* mrd_port_pins(uint8_t id);
const char* mrd_sensitivity_name(uint8_t id);
