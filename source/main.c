#include <3ds.h>
#include <stdio.h>

int main(void)
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	printf("\x1b[2;2HWiggle3DS - Toolchain-Test\n");
	printf("\x1b[4;2HWenn du das liest, klappt alles!\n");
	printf("\x1b[28;2HSTART = beenden");

	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;

		printf("\x1b[7;2H3D-Regler: %3d%%  ", (int)(osGet3DSliderState() * 100.0f));

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}

	gfxExit();
	return 0;
}
