#include <3ds.h>
#include <stdlib.h>
#include "camera.h"

#define FRAME_BYTES (CAM_W * CAM_H * 2)

static u16 *buf;
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
	buf = malloc(FRAME_BYTES * 2);
	if (!buf || R_FAILED(camInit()))
		return false;

	CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(SELECT_OUT1_OUT2, FRAME_RATE_15);
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
	CAMU_Activate(SELECT_NONE);
	running = false;
}

void camClose(void)
{
	camPause();
	closeEvent(0);
	closeEvent(1);
	camExit();
	free(buf);
	buf = NULL;
}

bool camPoll(void)
{
	if (!running)
		return false;

	if (!events[2])
		CAMU_SetReceiving(&events[2], buf, PORT_CAM1, FRAME_BYTES, (s16)transferUnit);
	if (!events[3])
		CAMU_SetReceiving(&events[3], (u8 *)buf + FRAME_BYTES, PORT_CAM2, FRAME_BYTES, (s16)transferUnit);
	if (interrupted)
	{
		CAMU_StartCapture(PORT_BOTH);
		interrupted = false;
	}

	s32 index = -1;
	svcWaitSynchronizationN(&index, events, 4, false, 0); // don't block the UI
	switch (index)
	{
	case 0:
	case 1:
		// Buffer error: drop the pending transfer and restart capture.
		closeEvent(index + 2);
		interrupted = true;
		return false;
	case 2:
	case 3:
		closeEvent(index);
		return true;
	default:
		return false;
	}
}

const u16 *camLeft(void) { return buf; }
const u16 *camRight(void) { return buf + CAM_W * CAM_H; }
