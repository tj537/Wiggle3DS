#include <3ds.h>
#include <stdlib.h>
#include "camera.h"

#define FRAME_BYTES (CAM_W * CAM_H * 2)
#define WAIT_NS 100000000LL // 100 ms, so pausing never waits long for the lock

// Two buffers per eye: the camera fills the back one while the UI reads the
// front one, so the preview never shows half a frame.
static u16 *bufs[2][2]; // [eye][buffer]
static volatile int front[2];
static u32 transferUnit;
// 0/1: buffer error events for CAM1/CAM2, 2/3: receive events for CAM1/CAM2.
static Handle events[4];

// A thread services the camera, so a transfer is re-armed the moment the
// previous one finishes, no matter what the UI is busy with. The lock
// guards all camera calls and `running`.
static Thread thread;
static LightLock lock;
static volatile bool running, quit, frameReady;
// Set while the UI thread wants the lock; the camera thread runs at a higher
// priority and would otherwise grab it right back.
static volatile bool uiWaiting;

static void closeEvent(int i)
{
	if (events[i])
	{
		svcCloseHandle(events[i]);
		events[i] = 0;
	}
}

static void arm(int eye)
{
	if (!events[2 + eye])
		CAMU_SetReceiving(&events[2 + eye], bufs[eye][front[eye] ^ 1], eye ? PORT_CAM2 : PORT_CAM1,
		                  FRAME_BYTES, (s16)transferUnit);
}

static void serviceCamera(void)
{
	arm(0);
	arm(1);
	s32 index = -1;
	// A timeout result isn't negative, so R_FAILED doesn't catch it; only an
	// exact success means an event fired.
	if (svcWaitSynchronizationN(&index, events, 4, false, WAIT_NS) != 0 || index < 0 || index > 3)
		return;
	if (index < 2)
	{
		// Buffer error: drop that eye's transfer and restart capture.
		closeEvent(index + 2);
		CAMU_StartCapture(PORT_BOTH);
		return;
	}
	closeEvent(index);
	front[index - 2] ^= 1;
	arm(index - 2);
	frameReady = true;
}

static void cameraThread(void *arg)
{
	(void)arg;
	while (!quit)
	{
		if (uiWaiting)
		{
			svcSleepThread(1000000LL);
			continue;
		}
		LightLock_Lock(&lock);
		bool active = running;
		if (active)
			serviceCamera();
		LightLock_Unlock(&lock);
		if (!active)
			svcSleepThread(10000000LL);
	}
}

bool camOpen(void)
{
	for (int i = 0; i < 4; i++)
		if (!(bufs[i / 2][i % 2] = calloc(1, FRAME_BYTES)))
			return false;
	if (R_FAILED(camInit()))
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

	// Runs above the UI thread so it gets the CPU as soon as a frame lands.
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	LightLock_Init(&lock);
	quit = false;
	thread = threadCreate(cameraThread, NULL, 0x4000, prio > 0x18 ? prio - 1 : prio, -2, false);
	return thread != NULL;
}

static void lockFromUi(void)
{
	uiWaiting = true;
	LightLock_Lock(&lock);
	uiWaiting = false;
}

void camResume(void)
{
	lockFromUi();
	if (!running)
	{
		CAMU_Activate(SELECT_OUT1_OUT2);
		CAMU_ClearBuffer(PORT_BOTH);
		CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
		CAMU_StartCapture(PORT_BOTH);
		running = true;
	}
	LightLock_Unlock(&lock);
}

void camPause(void)
{
	lockFromUi();
	if (running)
	{
		CAMU_StopCapture(PORT_BOTH);
		closeEvent(2);
		closeEvent(3);
		CAMU_Activate(SELECT_NONE);
		running = false;
	}
	LightLock_Unlock(&lock);
}

void camClose(void)
{
	camPause();
	if (thread)
	{
		quit = true;
		threadJoin(thread, U64_MAX);
		threadFree(thread);
		thread = NULL;
	}
	closeEvent(0);
	closeEvent(1);
	camExit();
	for (int i = 0; i < 4; i++)
	{
		free(bufs[i / 2][i % 2]);
		bufs[i / 2][i % 2] = NULL;
	}
}

bool camPoll(void)
{
	bool ready = frameReady;
	frameReady = false;
	return ready;
}

const u16 *camLeft(void) { return bufs[0][front[0]]; }
const u16 *camRight(void) { return bufs[1][front[1]]; }
