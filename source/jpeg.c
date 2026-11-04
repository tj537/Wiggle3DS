#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jpeg.h"

// The standard tables from Annex K of the JPEG spec.
static const u8 zigzag[64] = {
	0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
	41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
	30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};
static const u8 quantLuma[64] = {
	16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,  14, 13, 16, 24,  40,  57,
	69, 56, 14, 17, 22,  29,  51,  87,  80, 62, 18, 22, 37,  56,  68,  109, 103, 77, 24, 35, 55,  64,
	81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};
static const u8 quantChroma[64] = {
	17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
	99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};
static const u8 dcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const u8 dcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const u8 dcVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const u8 acLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const u8 acLumaVals[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
	0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
	0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
	0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
	0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
static const u8 acChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const u8 acChromaVals[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
	0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
	0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
	0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
	0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

typedef struct
{
	u16 code[256];
	u8 size[256];
} Huff;

typedef struct
{
	FILE *f;
	u8 buf[16384];
	int len;
	u32 bits;
	int nbits;
	bool ok;
} Out;

static void flushOut(Out *o)
{
	if (o->len && fwrite(o->buf, 1, o->len, o->f) != (size_t)o->len)
		o->ok = false;
	o->len = 0;
}

static void putByte(Out *o, u8 b)
{
	o->buf[o->len++] = b;
	if (o->len == sizeof(o->buf))
		flushOut(o);
}

static void putBytes(Out *o, const u8 *p, int n)
{
	while (n--)
		putByte(o, *p++);
}

static void putWord(Out *o, int w)
{
	putByte(o, w >> 8);
	putByte(o, w & 0xFF);
}

// Entropy-coded data: a 0xFF byte is followed by a stuffed 0.
static void putBits(Out *o, u32 code, int size)
{
	o->bits = o->bits << size | (code & ((1u << size) - 1));
	o->nbits += size;
	while (o->nbits >= 8)
	{
		u8 b = o->bits >> (o->nbits - 8);
		putByte(o, b);
		if (b == 0xFF)
			putByte(o, 0);
		o->nbits -= 8;
	}
}

static void buildHuff(Huff *h, const u8 *bits, const u8 *vals)
{
	int code = 0, k = 0;
	for (int len = 1; len <= 16; len++)
	{
		for (int i = 0; i < bits[len - 1]; i++, k++)
		{
			h->code[vals[k]] = code++;
			h->size[vals[k]] = len;
		}
		code <<= 1;
	}
}

static void putHuffTable(Out *o, int id, const u8 *bits, const u8 *vals, int count)
{
	putByte(o, id);
	putBytes(o, bits, 16);
	putBytes(o, vals, count);
}

static void scaleQuant(u8 *dst, const u8 *base, int quality)
{
	int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
	for (int i = 0; i < 64; i++)
	{
		int q = (base[i] * scale + 50) / 100;
		dst[i] = q < 1 ? 1 : q > 255 ? 255 : q;
	}
}

static float cosTab[8][8]; // [u][x]: C(u)/2 * cos((2x+1)u pi/16)

// Forward DCT, quantization, then Huffman coding of one 8x8 block.
// quant: the reciprocals of the quantization table (multiplying is much
// faster than dividing on the 3DS).
static void codeBlock(Out *o, const float *in, const float *quant, int *dc, const Huff *dcH, const Huff *acH)
{
	float tmp[64];
	int coef[64];
	for (int y = 0; y < 8; y++)
		for (int u = 0; u < 8; u++)
		{
			float s = 0;
			for (int x = 0; x < 8; x++)
				s += in[y * 8 + x] * cosTab[u][x];
			tmp[y * 8 + u] = s;
		}
	for (int v = 0; v < 8; v++)
		for (int u = 0; u < 8; u++)
		{
			float s = 0;
			for (int y = 0; y < 8; y++)
				s += tmp[y * 8 + u] * cosTab[v][y];
			float q = s * quant[v * 8 + u];
			coef[v * 8 + u] = (int)(q < 0 ? q - 0.5f : q + 0.5f);
		}

	int diff = coef[0] - *dc, size = 0;
	*dc = coef[0];
	for (int a = diff < 0 ? -diff : diff; a; a >>= 1)
		size++;
	putBits(o, dcH->code[size], dcH->size[size]);
	if (size)
		putBits(o, diff < 0 ? diff - 1 : diff, size);

	int run = 0;
	for (int k = 1; k < 64; k++)
	{
		int c = coef[zigzag[k]];
		if (!c)
		{
			run++;
			continue;
		}
		while (run >= 16)
		{
			putBits(o, acH->code[0xF0], acH->size[0xF0]);
			run -= 16;
		}
		size = 0;
		for (int a = c < 0 ? -c : c; a; a >>= 1)
			size++;
		int sym = run << 4 | size;
		putBits(o, acH->code[sym], acH->size[sym]);
		putBits(o, c < 0 ? c - 1 : c, size);
		run = 0;
	}
	if (run)
		putBits(o, acH->code[0], acH->size[0]); // end of block
}

bool jpegWrite(const char *path, const u16 *img, int stride, int w, int h, int quality)
{
	static Out o;
	o = (Out){.f = fopen(path, "wb"), .ok = true};
	if (!o.f)
		return false;
	for (int u = 0; u < 8; u++)
		for (int x = 0; x < 8; x++)
			cosTab[u][x] = (u ? 0.5f : 0.5f * 0.70710678f) * cosf((2 * x + 1) * u * 3.14159265f / 16);
	u8 ql[64], qc[64];
	scaleQuant(ql, quantLuma, quality);
	scaleQuant(qc, quantChroma, quality);
	float rl[64], rc[64];
	for (int k = 0; k < 64; k++)
	{
		rl[k] = 1.0f / ql[k];
		rc[k] = 1.0f / qc[k];
	}
	static Huff dcL, acL, dcC, acC;
	buildHuff(&dcL, dcLumaBits, dcVals);
	buildHuff(&acL, acLumaBits, acLumaVals);
	buildHuff(&dcC, dcChromaBits, dcVals);
	buildHuff(&acC, acChromaBits, acChromaVals);

	static const u8 head[] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
	putBytes(&o, head, sizeof(head));
	putWord(&o, 0xFFDB);
	putWord(&o, 2 + 2 * 65);
	putByte(&o, 0);
	for (int k = 0; k < 64; k++)
		putByte(&o, ql[zigzag[k]]);
	putByte(&o, 1);
	for (int k = 0; k < 64; k++)
		putByte(&o, qc[zigzag[k]]);
	const u8 sof[] = {0xFF, 0xC0, 0, 17, 8, h >> 8, h & 0xFF, w >> 8, w & 0xFF, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1};
	putBytes(&o, sof, sizeof(sof));
	putWord(&o, 0xFFC4);
	putWord(&o, 2 + 4 * 17 + 12 + 162 + 12 + 162);
	putHuffTable(&o, 0x00, dcLumaBits, dcVals, 12);
	putHuffTable(&o, 0x10, acLumaBits, acLumaVals, 162);
	putHuffTable(&o, 0x01, dcChromaBits, dcVals, 12);
	putHuffTable(&o, 0x11, acChromaBits, acChromaVals, 162);
	static const u8 sos[] = {0xFF, 0xDA, 0, 12, 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
	putBytes(&o, sos, sizeof(sos));

	// 16x16 macroblocks: four luma blocks, then one each of averaged Cb, Cr.
	int dcY = 0, dcCb = 0, dcCr = 0;
	for (int my = 0; my < h; my += 16)
		for (int mx = 0; mx < w; mx += 16)
		{
			float Y[256], Cb[64] = {0}, Cr[64] = {0}, blk[64];
			for (int y = 0; y < 16; y++)
				for (int x = 0; x < 16; x++)
				{
					int sx = mx + x < w ? mx + x : w - 1, sy = my + y < h ? my + y : h - 1;
					u16 p = img[sy * stride + sx];
					int r5 = p >> 11, g6 = p >> 5 & 63, b5 = p & 31;
					float r = r5 << 3 | r5 >> 2, g = g6 << 2 | g6 >> 4, b = b5 << 3 | b5 >> 2;
					Y[y * 16 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
					int c = (y / 2) * 8 + x / 2;
					Cb[c] += 0.25f * (-0.168736f * r - 0.331264f * g + 0.5f * b);
					Cr[c] += 0.25f * (0.5f * r - 0.418688f * g - 0.081312f * b);
				}
			for (int by = 0; by < 2; by++)
				for (int bx = 0; bx < 2; bx++)
				{
					for (int y = 0; y < 8; y++)
						memcpy(blk + y * 8, Y + (by * 8 + y) * 16 + bx * 8, 8 * sizeof(float));
					codeBlock(&o, blk, rl, &dcY, &dcL, &acL);
				}
			codeBlock(&o, Cb, rc, &dcCb, &dcC, &acC);
			codeBlock(&o, Cr, rc, &dcCr, &dcC, &acC);
		}
	if (o.nbits)
		putBits(&o, 0x7F, 7); // pad the last byte with ones
	putWord(&o, 0xFFD9);
	flushOut(&o);
	bool ok = o.ok;
	if (fclose(o.f) != 0)
		ok = false;
	return ok;
}
