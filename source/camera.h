#pragma once
#include <3ds/types.h>

#define CAM_W 640
#define CAM_H 480

// Both outer cameras, streamed as RGB565 at 640x480 (VGA), 20 fps.
bool camOpen(void);
void camResume(void);
void camPause(void);
void camClose(void);

// Handles pending camera events without waiting; true when a new frame is
// complete. camLeft/camRight then point to it, until the next one.
bool camPoll(void);

const u16 *camLeft(void);
const u16 *camRight(void);
