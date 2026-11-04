#pragma once
#include <3ds/types.h>

// A tiny web server for getting the GIFs onto a phone over WiFi: a gallery
// page at http://<3ds-ip>:8080/ with small pictures (made on demand and
// kept in Cache/), and the GIFs, photos, videos and 3D files themselves.
// Runs only while the share screen is open.
#define SHARE_PORT 8080

// Fills `url` with the page address; false when there is no WiFi.
bool shareStart(char *url, int size);
// Answers the waiting requests (a few at most); true when a whole file was sent.
bool sharePoll(void);
void shareStop(void);
// Page colors as 0xRRGGBB: background, card, muted text, accent gradient.
void shareSetColors(u32 bg, u32 card, u32 dim, u32 acc0, u32 acc1);
// Only these shots on the page (picked in the gallery); count 0: all.
void shareSetOnly(const int *nums, int count);
