#pragma once

// Shutter sound through the DSP. Without the DSP firmware dump
// (sdmc:/3ds/dspfirm.cdc) init fails and the app stays silent.
void soundInit(void);
void soundExit(void);
void soundShutter(void);
