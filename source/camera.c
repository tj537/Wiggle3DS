#include <3ds.h>
#include <stdlib.h>
#include "camera.h"

#define FRAME_BYTES (CAM_W * CAM_H * 2)
#define WAIT_NS 100000000LL // 100 ms, so pausing never waits long for the lock

// Three buffers per eye: one the UI shows, one with the newest complete
// frame, and one the camera writes into. The camera never touches a buffer
// the UI might be reading, so the preview never tears.
static u16 *bufs[2][3]; // [eye][buffer]
static int shown[2], ready[2], writing[2] = {1, 1};
static LightLock swapLock; // guards shown/ready/writing
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
		CAMU_SetReceiving(&events[2 + eye], bufs[eye][writing[eye]], eye ? PORT_CAM2 : PORT_CAM1,
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
	int eye = index - 2;
	closeEvent(index);
	LightLock_Lock(&swapLock);
	ready[eye] = writing[eye];
	writing[eye] = 3 - ready[eye] - shown[eye]; // the buffer that is neither
	LightLock_Unlock(&swapLock);
	arm(eye);
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

static void lockFromUi(void)
{
	uiWaiting = true;
	LightLock_Lock(&lock);
	uiWaiting = false;
}

static int fpsSetting = 20;

static CAMU_FrameRate frameRate(void)
{
	return fpsSetting <= 15 ? FRAME_RATE_15 : fpsSetting >= 30 ? FRAME_RATE_30 : FRAME_RATE_20;
}

void camSetFps(int fps)
{
	lockFromUi();
	fpsSetting = fps;
	if (running)
	{
		CAMU_StopCapture(PORT_BOTH);
		CAMU_SetFrameRate(SELECT_OUT1_OUT2, frameRate());
		CAMU_ClearBuffer(PORT_BOTH);
		CAMU_StartCapture(PORT_BOTH);
	}
	else
		CAMU_SetFrameRate(SELECT_OUT1_OUT2, frameRate());
	LightLock_Unlock(&lock);
}

bool camOpen(void)
{
	for (int i = 0; i < 6; i++)
		if (!(bufs[i / 3][i % 3] = calloc(1, FRAME_BYTES)))
			return false;
	if (R_FAILED(camInit()))
		return false;

	CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(SELECT_OUT1_OUT2, frameRate());
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
	LightLock_Init(&swapLock);
	quit = false;
	thread = threadCreate(cameraThread, NULL, 0x4000, prio > 0x18 ? prio - 1 : prio, -2, false);
	return thread != NULL;
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
	for (int i = 0; i < 6; i++)
	{
		free(bufs[i / 3][i % 3]);
		bufs[i / 3][i % 3] = NULL;
	}
}

bool camPoll(void)
{
	if (!frameReady)
		return false;
	frameReady = false;
	LightLock_Lock(&swapLock);
	shown[0] = ready[0];
	shown[1] = ready[1];
	LightLock_Unlock(&swapLock);
	return true;
}

const u16 *camLeft(void) { return bufs[0][shown[0]]; }
const u16 *camRight(void) { return bufs[1][shown[1]]; }
