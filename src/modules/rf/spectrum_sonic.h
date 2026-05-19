#ifndef __BRUCE_SPECTRUM_SONIC_H__
#define __BRUCE_SPECTRUM_SONIC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Spectrum analyzer audio (passive) ---- */
void sonicSpectrumTickRSSI(int rssi_dbm);
void sonicSpectrumTickScan(int rssi_dbm, float freq_mhz, int step_idx, int step_count);
void sonicSpectrumTickNRF(int level, int channel_idx, int channel_count);
void sonicSpectrumStop();

/* ---- Jammer audio (active transmission) ----
 *
 * sonicJamTickNRF: NRF24 channel-by-channel jamming.
 *   channel (0-125) drives base pitch.
 *   strategy: 0=CW, 1=flood, 2=turbo flood. Higher = more agitated warble.
 *
 * sonicJamTickFreq: CC1101 freq-sweep jamming.
 *   freq_mhz position within [min_mhz..max_mhz] drives base pitch.
 *   intensity (0..1) widens warble.
 *
 * sonicJamTickFixed: fixed-freq jamming (no channel/freq to track).
 *   Steady high pitch with `variance` adding warble spread.
 *
 * sonicJamStop: reset state on exit. */
void sonicJamTickNRF(int channel, int strategy);
void sonicJamTickFreq(float freq_mhz, float min_mhz, float max_mhz, float intensity);
void sonicJamTickFixed(int variant);
void sonicJamStop();

#ifdef __cplusplus
}
#endif

#endif
