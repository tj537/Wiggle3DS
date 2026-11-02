#pragma once
#include <3ds/types.h>

// GIFs are made in a background thread from the saved photos, so saving
// (and quick capture) returns right away. The job list is not kept on quit;
// exportQueueMissing picks up the shots without a GIF on the next start.
void exportStart(void);
void exportStop(void); // finishes the GIF in progress
void exportSetHalf(bool half); // export at half size
void exportSetSquare(bool square); // export a centered 1:1 crop
void exportSetPhotoEye(int eye); // photos (transition off): 0 left camera, 1 right
// (Re)makes the GIF of a saved shot; a shot already waiting is not added twice.
void exportQueue(int num);
// Called before exportStart, the check runs in the background thread.
void exportQueueMissing(const int *nums, int count);
// Queued and in-progress GIFs.
int exportPending(void);
bool exportIsPending(int num);
// Bumped whenever a GIF is done (or failed), to notice changes.
u32 exportGeneration(void);
// Shot whose GIF failed most recently, or 0.
int exportLastError(void);
