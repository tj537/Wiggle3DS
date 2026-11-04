#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp4.h"

#define LOOP_MS 6000
#define MAX_SAMPLES 400

// ---- H.264: bits, then NAL units with emulation prevention ----

typedef struct
{
	u8 *buf;
	int len, bit; // bytes done, bits used in buf[len]
} Bits;

static void putBit(Bits *b, int v)
{
	if (!b->bit)
		b->buf[b->len] = 0;
	if (v)
		b->buf[b->len] |= 0x80 >> b->bit;
	if (++b->bit == 8)
	{
		b->bit = 0;
		b->len++;
	}
}

static void putU(Bits *b, int n, u32 v)
{
	while (n--)
		putBit(b, v >> n & 1);
}

static void putUe(Bits *b, u32 v)
{
	int n = 0;
	while ((v + 1) >> (n + 1))
		n++;
	putU(b, n, 0);
	putU(b, n + 1, v + 1);
}

static void putSe(Bits *b, int v) { putUe(b, v > 0 ? 2 * v - 1 : -2 * v); }

static void align(Bits *b, int with)
{
	while (b->bit)
		putBit(b, with);
}

static void trailing(Bits *b)
{
	putBit(b, 1);
	align(b, 0);
}

// RBSP to NAL unit with its header byte; returns the size. 00 00 0x
// (x <= 3) gets a 03 in between, so no start code appears inside.
static int toNal(u8 *out, int header, const u8 *rbsp, int len)
{
	int n = 0, zeros = 0;
	out[n++] = header;
	for (int i = 0; i < len; i++)
	{
		if (zeros >= 2 && rbsp[i] <= 3)
		{
			out[n++] = 3;
			zeros = 0;
		}
		out[n++] = rbsp[i];
		zeros = rbsp[i] ? 0 : zeros + 1;
	}
	return n;
}

static int mbW(int w) { return (w + 15) / 16; }
static int mbH(int h) { return (h + 15) / 16; }

static int makeSps(u8 *out, int w, int h)
{
	u8 rbsp[64];
	Bits b = {rbsp, 0, 0};
	putU(&b, 8, 66);   // Baseline
	putU(&b, 8, 0xC0); // constraint_set0 and 1: constrained baseline
	putU(&b, 8, 42);   // level 4.2: stored frames take a lot of bits
	putUe(&b, 0);      // sps id
	putUe(&b, 0);      // log2_max_frame_num - 4
	putUe(&b, 2);      // picture order = decoding order
	putUe(&b, 1);      // reference frames
	putBit(&b, 0);
	putUe(&b, mbW(w) - 1);
	putUe(&b, mbH(h) - 1);
	putBit(&b, 1); // frames only
	putBit(&b, 1); // direct_8x8_inference
	bool crop = w % 16 || h % 16;
	putBit(&b, crop);
	if (crop) // in chroma samples: 2 px each
	{
		putUe(&b, 0);
		putUe(&b, (mbW(w) * 16 - w) / 2);
		putUe(&b, 0);
		putUe(&b, (mbH(h) * 16 - h) / 2);
	}
	putBit(&b, 0); // no VUI: BT.601, limited range
	trailing(&b);
	return toNal(out, 0x67, rbsp, b.len);
}

static int makePps(u8 *out)
{
	u8 rbsp[16];
	Bits b = {rbsp, 0, 0};
	putUe(&b, 0); // pps id
	putUe(&b, 0); // sps id
	putBit(&b, 0); // CAVLC
	putBit(&b, 0);
	putUe(&b, 0); // one slice group
	putUe(&b, 0);
	putUe(&b, 0);
	putBit(&b, 0);
	putU(&b, 2, 0);
	putSe(&b, 0); // qp
	putSe(&b, 0);
	putSe(&b, 0); // chroma qp offset
	putBit(&b, 1); // deblocking control in the slice header
	putBit(&b, 0);
	putBit(&b, 0);
	trailing(&b);
	return toNal(out, 0x68, rbsp, b.len);
}

static u8 clamp8(int v) { return v < 1 ? 1 : v > 254 ? 254 : v; }

// One frame: an IDR slice of I_PCM macroblocks, BT.601 limited range.
static int makeSlice(u8 *out, u8 *rbsp, const GifFrame *f, int w, int h, int idrId)
{
	Bits b = {rbsp, 0, 0};
	putUe(&b, 0); // first macroblock
	putUe(&b, 7); // I slice, all of them
	putUe(&b, 0); // pps id
	putU(&b, 4, 0); // frame_num
	putUe(&b, idrId);
	putBit(&b, 0); // no_output_of_prior_pics
	putBit(&b, 0); // long_term_reference
	putSe(&b, 0); // qp delta
	putUe(&b, 1); // no deblocking: the samples are exact
	for (int my = 0; my < mbH(h); my++)
		for (int mx = 0; mx < mbW(w); mx++)
		{
			putUe(&b, 25); // I_PCM
			align(&b, 0);
			u8 *y = rbsp + b.len, *cb = y + 256, *cr = cb + 64;
			int sumB[64] = {0}, sumR[64] = {0};
			for (int j = 0; j < 16; j++)
				for (int i = 0; i < 16; i++)
				{
					int sx = mx * 16 + i, sy = my * 16 + j;
					sx = sx < w ? sx : w - 1;
					sy = sy < h ? sy : h - 1;
					u16 p = f->img[(f->y + sy) * f->stride + f->x + sx];
					int r = (p >> 11) << 3 | p >> 13, g = (p >> 5 & 63) << 2 | (p >> 9 & 3), bl = (p & 31) << 3 | (p >> 2 & 7);
					y[j * 16 + i] = clamp8(((66 * r + 129 * g + 25 * bl + 128) >> 8) + 16);
					int c = (j / 2) * 8 + i / 2;
					sumB[c] += -38 * r - 74 * g + 112 * bl;
					sumR[c] += 112 * r - 94 * g - 18 * bl;
				}
			for (int c = 0; c < 64; c++)
			{
				cb[c] = clamp8(((sumB[c] / 4 + 128) >> 8) + 128);
				cr[c] = clamp8(((sumR[c] / 4 + 128) >> 8) + 128);
			}
			b.len += 384;
		}
	trailing(&b);
	return toNal(out, 0x65, rbsp, b.len);
}

// ---- MP4 boxes ----

static void be32(FILE *f, u32 v)
{
	u8 x[4] = {v >> 24, v >> 16, v >> 8, v};
	fwrite(x, 1, 4, f);
}
static void be16(FILE *f, u32 v)
{
	u8 x[2] = {v >> 8, v};
	fwrite(x, 1, 2, f);
}
static void zeros(FILE *f, int n)
{
	while (n--)
		fputc(0, f);
}

// Boxes are written with a placeholder size, patched in `close`.
static long boxOpen(FILE *f, const char *type)
{
	long at = ftell(f);
	be32(f, 0);
	fwrite(type, 1, 4, f);
	return at;
}

static void boxClose(FILE *f, long at)
{
	long end = ftell(f);
	fseek(f, at, SEEK_SET);
	be32(f, (u32)(end - at));
	fseek(f, end, SEEK_SET);
}

static void matrix(FILE *f)
{
	static const u32 m[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};
	for (int i = 0; i < 9; i++)
		be32(f, m[i]);
}

bool mp4Write(const char *path, const GifFrame *frames, int count, int w, int h, int delayMs)
{
	w &= ~1; // 4:2:0 crops in steps of two
	h &= ~1;
	if (count < 1 || w < 16 || h < 16 || delayMs <= 0)
		return false;
	int loops = (LOOP_MS + count * delayMs - 1) / (count * delayMs), samples = loops * count;
	samples = samples > MAX_SAMPLES ? MAX_SAMPLES / count * count : samples;
	FILE *f = fopen(path, "wb");
	int mbs = mbW(w) * mbH(h), cap = mbs * 400 + 64;
	u8 *rbsp = malloc(cap), *nal = malloc(cap * 3 / 2);
	u32 *offset = calloc(count, 4), *size = calloc(count, 4);
	bool ok = f && rbsp && nal && offset && size;

	u8 sps[64], pps[16];
	int spsLen = makeSps(sps, w, h), ppsLen = makePps(pps);
	if (ok)
	{
		long ftyp = boxOpen(f, "ftyp");
		fwrite("isom", 1, 4, f);
		be32(f, 0x200);
		fwrite("isomiso2avc1mp41", 1, 16, f);
		boxClose(f, ftyp);
		long mdat = boxOpen(f, "mdat");
		// Each distinct frame once; a repeat points to the first.
		for (int i = 0; i < count && ok; i++)
		{
			int same = -1;
			for (int j = 0; j < i; j++)
				if (frames[j].img == frames[i].img && frames[j].x == frames[i].x && frames[j].y == frames[i].y)
					same = j;
			if (same >= 0)
			{
				offset[i] = offset[same];
				size[i] = size[same];
				continue;
			}
			int n = makeSlice(nal, rbsp, &frames[i], w, h, i);
			offset[i] = (u32)ftell(f);
			size[i] = 4 + n;
			be32(f, n);
			ok = fwrite(nal, 1, n, f) == (size_t)n;
		}
		boxClose(f, mdat);
	}
	if (ok)
	{
		u32 duration = samples * delayMs;
		long moov = boxOpen(f, "moov");
		long mvhd = boxOpen(f, "mvhd");
		be32(f, 0);
		be32(f, 0);
		be32(f, 0);
		be32(f, 1000);
		be32(f, duration);
		be32(f, 0x10000);
		be16(f, 0x100);
		zeros(f, 10);
		matrix(f);
		zeros(f, 24);
		be32(f, 2);
		boxClose(f, mvhd);
		long trak = boxOpen(f, "trak");
		long tkhd = boxOpen(f, "tkhd");
		be32(f, 3); // enabled, in the movie
		be32(f, 0);
		be32(f, 0);
		be32(f, 1);
		be32(f, 0);
		be32(f, duration);
		zeros(f, 16);
		matrix(f);
		be32(f, w << 16);
		be32(f, h << 16);
		boxClose(f, tkhd);
		long mdia = boxOpen(f, "mdia");
		long mdhd = boxOpen(f, "mdhd");
		be32(f, 0);
		be32(f, 0);
		be32(f, 0);
		be32(f, 1000);
		be32(f, duration);
		be16(f, 0x55C4); // "und"
		be16(f, 0);
		boxClose(f, mdhd);
		long hdlr = boxOpen(f, "hdlr");
		be32(f, 0);
		be32(f, 0);
		fwrite("vide", 1, 4, f);
		zeros(f, 12);
		fwrite("Wiggle3DS", 1, 10, f);
		boxClose(f, hdlr);
		long minf = boxOpen(f, "minf");
		long vmhd = boxOpen(f, "vmhd");
		be32(f, 1);
		zeros(f, 8);
		boxClose(f, vmhd);
		long dinf = boxOpen(f, "dinf"), dref = boxOpen(f, "dref");
		be32(f, 0);
		be32(f, 1);
		long url = boxOpen(f, "url ");
		be32(f, 1); // in this file
		boxClose(f, url);
		boxClose(f, dref);
		boxClose(f, dinf);
		long stbl = boxOpen(f, "stbl");
		long stsd = boxOpen(f, "stsd");
		be32(f, 0);
		be32(f, 1);
		long avc1 = boxOpen(f, "avc1");
		zeros(f, 6);
		be16(f, 1);
		zeros(f, 16);
		be16(f, w);
		be16(f, h);
		be32(f, 0x480000);
		be32(f, 0x480000);
		be32(f, 0);
		be16(f, 1);
		zeros(f, 32);
		be16(f, 0x18);
		be16(f, 0xFFFF);
		long avcC = boxOpen(f, "avcC");
		fputc(1, f);
		fputc(66, f);
		fputc(0xC0, f);
		fputc(42, f);
		fputc(0xFF, f); // 4 byte lengths
		fputc(0xE1, f); // one SPS
		be16(f, spsLen);
		fwrite(sps, 1, spsLen, f);
		fputc(1, f);
		be16(f, ppsLen);
		fwrite(pps, 1, ppsLen, f);
		boxClose(f, avcC);
		boxClose(f, avc1);
		boxClose(f, stsd);
		long stts = boxOpen(f, "stts");
		be32(f, 0);
		be32(f, 1);
		be32(f, samples);
		be32(f, delayMs);
		boxClose(f, stts);
		long stsc = boxOpen(f, "stsc"); // one sample per chunk
		be32(f, 0);
		be32(f, 1);
		be32(f, 1);
		be32(f, 1);
		be32(f, 1);
		boxClose(f, stsc);
		long stsz = boxOpen(f, "stsz");
		be32(f, 0);
		be32(f, 0);
		be32(f, samples);
		for (int i = 0; i < samples; i++)
			be32(f, size[i % count]);
		boxClose(f, stsz);
		long stco = boxOpen(f, "stco");
		be32(f, 0);
		be32(f, samples);
		for (int i = 0; i < samples; i++)
			be32(f, offset[i % count]);
		boxClose(f, stco);
		boxClose(f, stbl);
		boxClose(f, minf);
		boxClose(f, mdia);
		boxClose(f, trak);
		boxClose(f, moov);
		ok = !ferror(f);
	}
	if (f && fclose(f) != 0)
		ok = false;
	free(rbsp);
	free(nal);
	free(offset);
	free(size);
	return ok;
}
