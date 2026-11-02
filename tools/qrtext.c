// Host tool: prints the QR code of argv[1] as lines of '#' (dark) and ' ' (light), no quiet zone.
// Built by tools/send-cia.py with the same qrcodegen the app uses.
#include <stdio.h>
#include "qrcodegen.h"

int main(int argc, char **argv) {
	static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
	if (argc < 2 || !qrcodegen_encodeText(argv[1], tmp, qr, qrcodegen_Ecc_MEDIUM,
	                                      qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
		return 1;
	int n = qrcodegen_getSize(qr);
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) putchar(qrcodegen_getModule(qr, x, y) ? '#' : ' ');
		putchar('\n');
	}
	return 0;
}
