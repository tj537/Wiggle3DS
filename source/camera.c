#include <3ds.h>
#include <stdlib.h>
#include "camera.h"

#define FRAME_BYTES (CAM_W * CAM_H * 2)

// Two frame buffers (each holding both eyes): the camera fills the back one
// while the UI reads the front one, so the preview never shows half a frame.
static u16 *bufs[2];
static int front;
static bool got[2]; // eye already received into the back buffer
static u32 transferUnit;
// 0/1: buffer error events for CAM1/CAM2, 2/3: receive events for CAM1/CAM2.
static Handle events[4];
static bool running, interrupted;

static void closeEvent(int i)
{
	if (events[i])
	{
		svcCloseHandle(events[i]);
		events[i] = 0;
	}
}

bool camOpen(void)
{
	bufs[0] = calloc(1, FRAME_BYTES * 2);
	bufs[1] = calloc(1, FRAME_BYTES * 2);
	if (!bufs[0] || !bufs[1] || R_FAILED(camInit()))
		return false;

	CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(SELECT_OUT1_OUT2, FRAME_RATE_20);
	CAMU_SetNoiseFilter(SELECT_OUT1_OUT2, true);
	CAMU_SetAutoExposure(SELECT_OUT1_OUT2, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1_OUT2, true);
	CAMU_SetTrimming(PORT_CAM1, false);
	CAMU_SetTrimming(PORT_CAM2, false);

	CAMU_GetMaxBytes(&transferUnit, CAM_W, CAM_H);
	CAMU_SetTransferBytes(PORT_BOTH, transferUnit, CAM_W, CAM_H);
	CAMU_GetBufferErrorInterruptEvent(&events[0], PORT_CAM1);
	CAMU_GetBufferErrorInterruptEvent(&events[1], PORT_CAM2);
	return true;
}

void camResume(void)
{
	if (running)
		return;
	CAMU_Activate(SELECT_OUT1_OUT2);
	CAMU_ClearBuffer(PORT_BOTH);
	CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
	CAMU_StartCapture(PORT_BOTH);
	running = true;
	interrupted = false;
}

void camPause(void)
{
	if (!running)
		return;
	CAMU_StopCapture(PORT_BOTH);
	closeEvent(2);
	closeEvent(3);
	got[0] = got[1] = false;
	CAMU_Activate(SELECT_NONE);
	running = false;
}

void camClose(void)
{
	camPause();
	closeEvent(0);
	closeEvent(1);
	camExit();
	free(bufs[0]);
	free(bufs[1]);
	bufs[0] = bufs[1] = NULL;
}

bool camPoll(void)
{
	if (!running)
		return false;

	// An eye is re-armed only after the frame swap, so it can't overwrite
	// its half of the back buffer while the other eye is still arriving.
	u16 *back = bufs[front ^ 1];
	if (!events[2] && !got[0])
		CAMU_SetReceiving(&events[2], back, PORT_CAM1, FRAME_BYTES, (s16)transferUnit);
	if (!events[3] && !got[1])
		CAMU_SetReceiving(&events[3], back + CAM_W * CAM_H, PORT_CAM2, FRAME_BYTES, (s16)transferUnit);
	if (interrupted)
	{
		CAMU_StartCapture(PORT_BOTH);
		interrupted = false;
	}

	Handle wait[4];
	int map[4], n = 0;
	for (int i = 0; i < 4; i++)
		if (events[i])
		{
			wait[n] = events[i];
			map[n++] = i;
		}
	s32 index = -1;
	if (!n || R_FAILED(svcWaitSynchronizationN(&index, wait, n, false, 0)) || index < 0 || index >= n)
		return false; // nothing pending; don't block the UI
	int ev = map[index];
	if (ev < 2)
	{
		// Buffer error: drop that eye's transfer and restart capture.
		closeEvent(ev + 2);
		got[ev] = false;
		interrupted = true;
		return false;
	}
	closeEvent(ev);
	got[ev - 2] = true;
	if (!got[0] || !got[1])
		return false;
	front ^= 1;
	got[0] = got[1] = false;
	return true;
}

const u16 *camLeft(void) { return bufs[front]; }
const u16 *camRight(void) { return bufs[front] + CAM_W * CAM_H; }
