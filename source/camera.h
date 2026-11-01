#pragma once
#include <3ds/types.h>

#define CAM_W 400
#define CAM_H 240

// Both outer cameras, streamed as RGB565 at 400x240.
bool camOpen(void);
void camResume(void);
void camPause(void);
void camClose(void);

// Waits for the next camera event; returns true when a new frame arrived.
bool camPoll(void);

const u16 *camLeft(void);
const u16 *camRight(void);
