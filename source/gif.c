#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gif.h"

#define LEVELS_R 6
#define LEVELS_G 7
#define LEVELS_B 6
#define CLEAR_CODE 256
#define EOI_CODE 257
#define MAX_CODES 4096

static const u8 bayer[4][4] = {
	{0, 8, 2, 10},
	{12, 4, 14, 6},
	{3, 11, 1, 9},
	{15, 7, 13, 5},
};

static inline int quant(int v, int levels, int t) { return (v * (levels - 1) + t) / 255; }

static void quantize(u8 *dst, const GifFrame *f, int w, int h)
{
	for (int y = 0; y < h; y++)
	{
		const u16 *row = f->img + (f->y + y) * f->stride + f->x;
		for (int x = 0; x < w; x++)
		{
			u16 p = row[x];
			int r5 = p >> 11, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
			int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
			int t = (bayer[y & 3][x & 3] * 2 + 1) * 255 / 32;
			*dst++ = quant(r, LEVELS_R, t) * LEVELS_G * LEVELS_B + quant(g, LEVELS_G, t) * LEVELS_B + quant(b, LEVELS_B, t);
		}
	}
}

// LZW output, packed LSB-first into 255-byte GIF sub-blocks.
typedef struct
{
	FILE *f;
	u32 acc;
	int bits;
	u8 block[255];
	int len;
} BitWriter;

static void flushBlock(BitWriter *bw)
{
	if (!bw->len)
		return;
	fputc(bw->len, bw->f);
	fwrite(bw->block, 1, bw->len, bw->f);
	bw->len = 0;
}

static void putCode(BitWriter *bw, int code, int size)
{
	bw->acc |= (u32)code << bw->bits;
	bw->bits += size;
	while (bw->bits >= 8)
	{
		bw->block[bw->len++] = bw->acc & 0xFF;
		if (bw->len == 255)
			flushBlock(bw);
		bw->acc >>= 8;
		bw->bits -= 8;
	}
}

// dict[prefix * 256 + pixel] = code of that string, 0 if not in the table.
static void lzwEncode(FILE *f, const u8 *pix, int n, u16 *dict)
{
	BitWriter bw = {.f = f};
	int size = 9, next = EOI_CODE + 1;
	memset(dict, 0, MAX_CODES * 256 * sizeof(u16));
	putCode(&bw, CLEAR_CODE, size);

	int prefix = pix[0];
	for (int i = 1; i < n; i++)
	{
		int key = prefix * 256 + pix[i];
		if (dict[key])
		{
			prefix = dict[key];
			continue;
		}
		putCode(&bw, prefix, size);
		dict[key] = next++;
		if (next == MAX_CODES)
		{
			putCode(&bw, CLEAR_CODE, size);
			memset(dict, 0, MAX_CODES * 256 * sizeof(u16));
			size = 9;
			next = EOI_CODE + 1;
		}
		else if (next > (1 << size) && size < 12)
			size++;
		prefix = pix[i];
	}
	putCode(&bw, prefix, size);
	// The decoder adds one more entry after the last code; follow its code size.
	if (next + 1 > (1 << size) && size < 12)
		size++;
	putCode(&bw, EOI_CODE, size);
	if (bw.bits)
		putCode(&bw, 0, 8 - bw.bits);
	flushBlock(&bw);
	fputc(0, f);
}

static void put16(FILE *f, int v)
{
	fputc(v & 0xFF, f);
	fputc(v >> 8, f);
}

bool gifWrite(const char *path, const GifFrame *frames, int count, int w, int h, int delayMs)
{
	u8 *pix = malloc(w * h);
	u16 *dict = malloc(MAX_CODES * 256 * sizeof(u16));
	FILE *f = fopen(path, "wb");
	bool ok = pix && dict && f;
	if (ok)
	{
		setvbuf(f, NULL, _IOFBF, 64 * 1024);
		fwrite("GIF89a", 1, 6, f);
		put16(f, w);
		put16(f, h);
		fputc(0xF7, f); // global color table, 256 entries
		fputc(0, f);
		fputc(0, f);
		for (int i = 0; i < 256; i++)
		{
			int r = i / (LEVELS_G * LEVELS_B), g = i / LEVELS_B % LEVELS_G, b = i % LEVELS_B;
			bool used = i < LEVELS_R * LEVELS_G * LEVELS_B;
			fputc(used ? r * 255 / (LEVELS_R - 1) : 0, f);
			fputc(used ? g * 255 / (LEVELS_G - 1) : 0, f);
			fputc(used ? b * 255 / (LEVELS_B - 1) : 0, f);
		}
		// Loop forever.
		fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, f);

		for (int i = 0; i < count; i++)
		{
			fwrite("\x21\xF9\x04\x04", 1, 4, f); // graphic control: keep previous frame
			put16(f, (delayMs + 5) / 10);
			fputc(0, f);
			fputc(0, f);
			fputc(0x2C, f); // image descriptor
			put16(f, 0);
			put16(f, 0);
			put16(f, w);
			put16(f, h);
			fputc(0, f);
			fputc(8, f); // LZW minimum code size
			quantize(pix, &frames[i], w, h);
			lzwEncode(f, pix, w * h, dict);
		}
		fputc(0x3B, f);
		ok = !ferror(f);
	}
	if (f)
		fclose(f);
	free(dict);
	free(pix);
	return ok;
}
