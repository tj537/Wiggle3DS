#pragma once
#include <3ds/types.h>

// A tiny web server for getting the GIFs onto a phone over WiFi: a gallery
// page at http://<3ds-ip>:8080/ and the GIFs themselves. Runs only while
// the share screen is open.
#define SHARE_PORT 8080

// Fills `url` with the page address; false when there is no WiFi.
bool shareStart(char *url, int size);
// Answers at most one waiting request; returns true when a GIF was sent.
bool sharePoll(void);
void shareStop(void);
