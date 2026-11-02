#include <3ds.h>
#include <math.h>
#include <string.h>
#include "sound.h"

#define RATE 22050
#define SAMPLES (RATE * 13 / 100) // 130 ms

static bool ok;
static s16 *pcm;
static ndspWaveBuf wave;

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
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {1.0f, 1.0f};
	ndspSetMasterVol(1.0f);
	ndspChnSetMix(0, mix);
	ok = true;
}

void soundExit(void)
{
	if (!ok)
		return;
	ndspChnReset(0);
	ndspExit();
	linearFree(pcm);
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
