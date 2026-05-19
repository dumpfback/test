/*
 * spectrum_sonic.cpp — Sonic Screwdriver sound for Bruce.
 *
 * Two purposes:
 *  1) Passive spectrum-analyzer audio (pitch tracks RSSI/activity)
 *  2) Active jamming audio (pitch tracks channel/freq during TX)
 *
 * All driven by Bruce's _tone() HAL. Each tick function self-throttles
 * so audio updates can't outrun the speaker's ability to play them.
 */

#include <Arduino.h>
#include <globals.h>
#include "spectrum_sonic.h"

extern void _tone(unsigned int frequency, unsigned long duration);
/* EXTENDED_SOUNDS_PATCH_APPLIED */
extern bool playSpectrumExitSound();

/* ---- Common ---- */
#define SONIC_TONE_MS            10

/* ---- CC1101 RSSI (fixed freq) ---- */
#define SONIC_THROTTLE_RSSI_MS   75
#define SONIC_RSSI_FLOOR        -95
#define SONIC_RSSI_CEIL         -40
#define SONIC_RSSI_BASE_HZ       900
#define SONIC_RSSI_GAIN_HZ      1600

/* ---- CC1101 range scan ---- */
#define SONIC_THROTTLE_SCAN_MS   40
#define SONIC_SCAN_BASE_HZ       800
#define SONIC_SCAN_BAND_HZ       700
#define SONIC_SCAN_GAIN_HZ      1400
#define SONIC_SCAN_WARBLE_HZ     180
#define SONIC_SCAN_WARBLE_GAIN   320

/* ---- NRF24 spectrum ---- */
#define SONIC_THROTTLE_NRF_MS    20
#define SONIC_NRF_LEVEL_FLOOR    0
#define SONIC_NRF_LEVEL_CEIL     80
#define SONIC_NRF_BASE_HZ        700
#define SONIC_NRF_BAND_HZ        900
#define SONIC_NRF_GAIN_HZ       1300
#define SONIC_NRF_WARBLE_HZ      160
#define SONIC_NRF_WARBLE_GAIN    280

/* ---- NRF24 jammer (active TX) ----
 * Jamming should feel more "aggressive" than passive scanning.
 * Higher base pitch, wider warble, faster updates. */
#define SONIC_JAM_NRF_THROTTLE_MS  30
#define SONIC_JAM_NRF_BASE_HZ      1200    /* steadier and higher than spectrum */
#define SONIC_JAM_NRF_BAND_HZ      1100    /* channel position adds pitch range */
#define SONIC_JAM_NRF_WARBLE_BASE  200     /* CW strategy warble                */
#define SONIC_JAM_NRF_WARBLE_FLOOD 450     /* flood strategy warble             */
#define SONIC_JAM_NRF_WARBLE_TURBO 700     /* turbo strategy — chaotic          */

/* ---- CC1101 freq-sweep jammer (active TX) ---- */
#define SONIC_JAM_FREQ_THROTTLE_MS 30
#define SONIC_JAM_FREQ_BASE_HZ     1100
#define SONIC_JAM_FREQ_RANGE_HZ    1200    /* full freq range adds this much pitch */
#define SONIC_JAM_FREQ_WARBLE_HZ   250

/* ---- CC1101 fixed-freq jammer (full/itmt/noise) ----
 * No frequency to track. Use a phase-rotating set of distinct pitches
 * that cycle through, giving an "alarming, chaotic, but distinctly
 * jamming" feel. `variant` lets caller drive the cycle. */
#define SONIC_JAM_FIXED_THROTTLE_MS 35

/* ---- State ---- */
static unsigned long s_last_rssi_ms = 0;
static unsigned long s_last_scan_ms = 0;
static unsigned long s_last_nrf_ms = 0;
static unsigned long s_last_jam_nrf_ms = 0;
static unsigned long s_last_jam_freq_ms = 0;
static unsigned long s_last_jam_fixed_ms = 0;
static bool s_alt = false;

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float rssiToUnit(int rssi_dbm) {
    int c = clampi(rssi_dbm, SONIC_RSSI_FLOOR, SONIC_RSSI_CEIL);
    return (float)(c - SONIC_RSSI_FLOOR) /
           (float)(SONIC_RSSI_CEIL - SONIC_RSSI_FLOOR);
}

static inline float nrfLevelToUnit(int level) {
    int c = clampi(level, SONIC_NRF_LEVEL_FLOOR, SONIC_NRF_LEVEL_CEIL);
    return (float)(c - SONIC_NRF_LEVEL_FLOOR) /
           (float)(SONIC_NRF_LEVEL_CEIL - SONIC_NRF_LEVEL_FLOOR);
}

/* ============================================================
 * Spectrum analyzer audio (unchanged from previous rounds)
 * ============================================================ */

void sonicSpectrumTickRSSI(int rssi_dbm) {
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_rssi_ms < SONIC_THROTTLE_RSSI_MS) return;
    s_last_rssi_ms = now;

    float t = rssiToUnit(rssi_dbm);
    float pitch = (float)SONIC_RSSI_BASE_HZ + (float)SONIC_RSSI_GAIN_HZ * t;
    s_alt = !s_alt;
    unsigned int hz = (unsigned int)(s_alt ? pitch : pitch + 80.0f + 200.0f * t);
    _tone(hz, SONIC_TONE_MS);
}

void sonicSpectrumTickScan(int rssi_dbm, float freq_mhz, int step_idx, int step_count) {
    (void)freq_mhz;
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_scan_ms < SONIC_THROTTLE_SCAN_MS) return;
    s_last_scan_ms = now;

    float t = rssiToUnit(rssi_dbm);
    float band = 0.0f;
    if (step_count > 1 && step_idx >= 0) {
        if (step_idx >= step_count) step_idx = step_count - 1;
        band = (float)step_idx / (float)(step_count - 1);
    }

    float pitch = (float)SONIC_SCAN_BASE_HZ
                + (float)SONIC_SCAN_BAND_HZ * band
                + (float)SONIC_SCAN_GAIN_HZ * t;
    float spread = (float)SONIC_SCAN_WARBLE_HZ + (float)SONIC_SCAN_WARBLE_GAIN * t;

    s_alt = !s_alt;
    unsigned int hz = (unsigned int)(s_alt ? pitch : pitch + spread);
    _tone(hz, SONIC_TONE_MS);
}

void sonicSpectrumTickNRF(int level, int channel_idx, int channel_count) {
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_nrf_ms < SONIC_THROTTLE_NRF_MS) return;
    s_last_nrf_ms = now;

    float t = nrfLevelToUnit(level);
    float band = 0.0f;
    if (channel_count > 1 && channel_idx >= 0) {
        if (channel_idx >= channel_count) channel_idx = channel_count - 1;
        band = (float)channel_idx / (float)(channel_count - 1);
    }

    float pitch = (float)SONIC_NRF_BASE_HZ
                + (float)SONIC_NRF_BAND_HZ * band
                + (float)SONIC_NRF_GAIN_HZ * t;
    float spread = (float)SONIC_NRF_WARBLE_HZ + (float)SONIC_NRF_WARBLE_GAIN * t;

    s_alt = !s_alt;
    unsigned int hz = (unsigned int)(s_alt ? pitch : pitch + spread);
    _tone(hz, SONIC_TONE_MS);
}

void sonicSpectrumStop() {
    /* SPECTRUM_EXIT_SOUND_FIX_APPLIED */ // exit sound now fired by caller, not here
    s_last_rssi_ms = 0;
    s_last_scan_ms = 0;
    s_last_nrf_ms = 0;
    s_alt = false;
}

/* ============================================================
 * Jammer audio (active transmission feedback)
 * ============================================================ */

void sonicJamTickNRF(int channel, int strategy) {
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_jam_nrf_ms < SONIC_JAM_NRF_THROTTLE_MS) return;
    s_last_jam_nrf_ms = now;

    /* 0..125 NRF channel → 0..1 band position. */
    int c = clampi(channel, 0, 125);
    float band = (float)c / 125.0f;

    float pitch = (float)SONIC_JAM_NRF_BASE_HZ + (float)SONIC_JAM_NRF_BAND_HZ * band;
    float warble;
    switch (strategy) {
        case 0:  warble = (float)SONIC_JAM_NRF_WARBLE_BASE;  break;
        case 1:  warble = (float)SONIC_JAM_NRF_WARBLE_FLOOD; break;
        default: warble = (float)SONIC_JAM_NRF_WARBLE_TURBO; break;
    }

    s_alt = !s_alt;
    unsigned int hz = (unsigned int)(s_alt ? pitch : pitch + warble);
    _tone(hz, SONIC_TONE_MS);
}

void sonicJamTickFreq(float freq_mhz, float min_mhz, float max_mhz, float intensity) {
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_jam_freq_ms < SONIC_JAM_FREQ_THROTTLE_MS) return;
    s_last_jam_freq_ms = now;

    float band = 0.0f;
    if (max_mhz > min_mhz) {
        float f = clampf(freq_mhz, min_mhz, max_mhz);
        band = (f - min_mhz) / (max_mhz - min_mhz);
    }
    float t = clampf(intensity, 0.0f, 1.0f);

    float pitch = (float)SONIC_JAM_FREQ_BASE_HZ + (float)SONIC_JAM_FREQ_RANGE_HZ * band;
    float warble = (float)SONIC_JAM_FREQ_WARBLE_HZ * (0.5f + 0.5f * t);

    s_alt = !s_alt;
    unsigned int hz = (unsigned int)(s_alt ? pitch : pitch + warble);
    _tone(hz, SONIC_TONE_MS);
}

void sonicJamTickFixed(int variant) {
    if (!bruceConfig.soundEnabled) return;
    unsigned long now = millis();
    if (now - s_last_jam_fixed_ms < SONIC_JAM_FIXED_THROTTLE_MS) return;
    s_last_jam_fixed_ms = now;

    /* Cycle through a small set of distinct alarming pitches keyed
     * by `variant` so a phase-rotating caller produces noticeable
     * audible variety without us tracking external state. */
    static const unsigned int kJamFixedPitches[6] = {
        1600, 2100, 1800, 2400, 1900, 2600
    };
    unsigned int hz = kJamFixedPitches[((unsigned)variant) % 6];
    s_alt = !s_alt;
    /* Add a slight warble for character. */
    if (s_alt) hz += 220;
    _tone(hz, SONIC_TONE_MS);
}

void sonicJamStop() {
    s_last_jam_nrf_ms = 0;
    s_last_jam_freq_ms = 0;
    s_last_jam_fixed_ms = 0;
    s_alt = false;
}
