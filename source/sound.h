#pragma once

// Shutter and interface sounds through the DSP. Without the DSP firmware dump
// (sdmc:/3ds/dspfirm.cdc) init fails and the app stays silent.
void soundInit(void);
void soundExit(void);
void soundShutter(void);
// Small interface sounds.
typedef enum { SOUND_CLICK, SOUND_TICK, SOUND_DONE, SOUND_COUNT } Sound;
void soundPlay(int sound);
