#include <3ds.h>
#include <math.h>
#include <string.h>
#include "sound.h"

#define RATE 22050
#define SAMPLES (RATE * 13 / 100) // 130 ms

static bool ok;
static s16 *pcm;
static ndspWaveBuf wave;

// UI sounds on a channel of their own, so they never cut the shutter.
#define UI_MAX (RATE / 5) // 200 ms
static s16 *ui[SOUND_COUNT];
static int uiLen[SOUND_COUNT];
static ndspWaveBuf uiWave;

// A soft sine note with a quick start and an exponential fade.
static void note(s16 *out, int from, int len, float hz, float level, float decay)
{
	for (int i = 0; i < len; i++)
	{
		float t = (float)i / RATE, env = (i < 40 ? i / 40.0f : 1) * expf(-t * decay);
		out[from + i] += (s16)(sinf(t * hz * 6.2832f) * env * level * 32000);
	}
}

static void synthUi(void)
{
	// Click: a tiny high tick. Tick: the self-timer's second. Done: two
	// rising notes when something is saved.
	uiLen[SOUND_CLICK] = RATE / 60;
	note(ui[SOUND_CLICK], 0, uiLen[SOUND_CLICK], 2600, 0.25f, 260);
	uiLen[SOUND_TICK] = RATE / 20;
	note(ui[SOUND_TICK], 0, uiLen[SOUND_TICK], 1320, 0.35f, 60);
	uiLen[SOUND_DONE] = RATE / 5;
	note(ui[SOUND_DONE], 0, RATE / 10, 880, 0.3f, 30);
	note(ui[SOUND_DONE], RATE / 12, RATE / 5 - RATE / 12, 1320, 0.3f, 20);
}

// Two mechanical clicks (shutter open and close): a short burst of noise
// with a fast decay, over a low thump.
static void synth(void)
{
	u32 seed = 12345;
	float lp = 0;
	for (int i = 0; i < SAMPLES; i++)
	{
		float v = 0;
		for (int c = 0; c < 2; c++)
		{
			int t = i - c * RATE * 7 / 100; // second click 70 ms later
			if (t < 0)
				continue;
			float env = 1.0f / (1.0f + t * 0.02f);
			env *= env * env;
			seed = seed * 1664525 + 1013904223;
			float noise = (int)(seed >> 16 & 0xFFFF) / 32768.0f - 1.0f;
			float thump = t < RATE / 50 ? (t & 64 ? -0.5f : 0.5f) * (1 - t * 50.0f / RATE) : 0;
			v += (noise + thump) * env * (c ? 0.7f : 1.0f);
		}
		lp += (v - lp) * 0.6f; // soften the hiss
		// Driven hard into a soft limit: loud, without harsh clipping.
		float s = tanhf(lp * 2.6f) * 32000;
		pcm[i] = (s16)s;
	}
}

void soundInit(void)
{
	if (R_FAILED(ndspInit()))
		return;
	if (!(pcm = linearAlloc(SAMPLES * sizeof(s16))))
	{
		ndspExit();
		return;
	}
	synth();
	DSP_FlushDataCache(pcm, SAMPLES * sizeof(s16));
	for (int i = 0; i < SOUND_COUNT; i++)
		if ((ui[i] = linearAlloc(UI_MAX * sizeof(s16))))
			memset(ui[i], 0, UI_MAX * sizeof(s16));
	if (ui[SOUND_COUNT - 1])
	{
		synthUi();
		for (int i = 0; i < SOUND_COUNT; i++)
			DSP_FlushDataCache(ui[i], UI_MAX * sizeof(s16));
	}
	ndspChnReset(1);
	ndspChnSetInterp(1, NDSP_INTERP_LINEAR);
	ndspChnSetRate(1, RATE);
	ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {1.0f, 1.0f};
	ndspSetMasterVol(1.0f);
	ndspChnSetMix(0, mix);
	ndspChnSetMix(1, mix);
	ok = true;
}

void soundExit(void)
{
	if (!ok)
		return;
	ndspChnReset(0);
	ndspChnReset(1);
	ndspExit();
	linearFree(pcm);
	for (int i = 0; i < SOUND_COUNT; i++)
	{
		linearFree(ui[i]);
		ui[i] = NULL;
	}
	ok = false;
}

void soundShutter(void)
{
	if (!ok)
		return;
	ndspChnWaveBufClear(0);
	memset(&wave, 0, sizeof(wave));
	wave.data_vaddr = pcm;
	wave.nsamples = SAMPLES;
	ndspChnWaveBufAdd(0, &wave);
}

void soundPlay(int sound)
{
	if (!ok || sound < 0 || sound >= SOUND_COUNT || !ui[sound])
		return;
	ndspChnWaveBufClear(1);
	memset(&uiWave, 0, sizeof(uiWave));
	uiWave.data_vaddr = ui[sound];
	uiWave.nsamples = uiLen[sound];
	ndspChnWaveBufAdd(1, &uiWave);
}
