#pragma once
#include "gif.h"

// The wiggle as an MP4 video (H.264), for messengers and phones that don't
// loop GIFs. Every frame is stored uncompressed (I_PCM macroblocks), which
// needs no encoder; frames that repeat (the same `img` again) are stored
// once and pointed to again, so the file stays a few MB. The loop repeats
// for about six seconds.
bool mp4Write(const char *path, const GifFrame *frames, int count, int w, int h, int delayMs);
