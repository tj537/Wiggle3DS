#pragma once
#include <3ds/types.h>

#define CAM_W 640
#define CAM_H 480

// Both outer cameras, streamed as RGB565 at 640x480 (VGA).
bool camOpen(void);
// Viewfinder frame rate: 15, 20 or 30.
void camSetFps(int fps);
void camResume(void);
void camPause(void);
void camClose(void);

// True when a new frame is complete; camLeft/camRight then point to it and
// stay valid (untouched by the camera) until the next successful poll.
bool camPoll(void);

const u16 *camLeft(void);
const u16 *camRight(void);
