#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gif.h"

#define CLEAR_CODE 256
#define EOI_CODE 257
#define MAX_CODES 4096
#define PAL_SIZE 256
#define KMEANS_ROUNDS 3

// Adaptive palette: median cut over the RGB565 histogram of all frames,
// refined with a few k-means rounds. Sources are RGB565, so a 64K lookup
// table maps every pixel exactly to its nearest palette entry.
typedef struct
{
	u8 c[3];
	u16 key;
	u32 n;
} Color;

typedef struct
{
	int start, end, channel;
	u64 score;
} Box;

static u32 hist[65536];
static Color colors[65536];
static u8 lut[65536];
static u8 pal[PAL_SIZE][3];
static int palCount;
static int sortChannel;

// Rough perceptual weights for r, g, b.
static const int weight[3] = {3, 4, 2};

static int cmpColor(const void *a, const void *b)
{
	return ((const Color *)a)->c[sortChannel] - ((const Color *)b)->c[sortChannel];
}

static void boxStats(Box *b)
{
	int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
	u64 n = 0;
	for (int i = b->start; i < b->end; i++)
	{
		for (int k = 0; k < 3; k++)
		{
			if (colors[i].c[k] < lo[k]) lo[k] = colors[i].c[k];
			if (colors[i].c[k] > hi[k]) hi[k] = colors[i].c[k];
		}
		n += colors[i].n;
	}
	b->channel = 0;
	int best = -1;
	for (int k = 0; k < 3; k++)
	{
		int r = (hi[k] - lo[k]) * weight[k];
		if (r > best)
		{
			best = r;
			b->channel = k;
		}
	}
	b->score = b->end - b->start > 1 ? n * (u64)best * best : 0;
}

static int nearest(const u8 *c)
{
	int best = 0;
	u32 bestDist = 0xFFFFFFFF;
	for (int i = 0; i < palCount; i++)
	{
		int dr = c[0] - pal[i][0], dg = c[1] - pal[i][1], db = c[2] - pal[i][2];
		u32 d = weight[0] * dr * dr + weight[1] * dg * dg + weight[2] * db * db;
		if (d < bestDist)
		{
			bestDist = d;
			best = i;
		}
	}
	return best;
}

static void buildPalette(const GifFrame *frames, int count, int w, int h)
{
	memset(hist, 0, sizeof(hist));
	for (int i = 0; i < count; i++)
		for (int y = 0; y < h; y++)
		{
			const u16 *row = frames[i].img + (frames[i].y + y) * frames[i].stride + frames[i].x;
			const u16 *recolor = frames[i].lut;
			for (int x = 0; x < w; x++)
				hist[recolor ? recolor[row[x]] : row[x]]++;
		}

	int n = 0;
	for (int k = 0; k < 65536; k++)
		if (hist[k])
		{
			int r5 = k >> 11, g6 = (k >> 5) & 0x3F, b5 = k & 0x1F;
			colors[n] = (Color){{(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)}, k, hist[k]};
			n++;
		}

	static Box boxes[PAL_SIZE];
	int boxCount = 1;
	boxes[0] = (Box){0, n, 0, 0};
	boxStats(&boxes[0]);
	while (boxCount < PAL_SIZE)
	{
		int bi = 0;
		for (int i = 1; i < boxCount; i++)
			if (boxes[i].score > boxes[bi].score)
				bi = i;
		Box *b = &boxes[bi];
		if (!b->score)
			break;

		// Split at the weighted median of the widest channel.
		sortChannel = b->channel;
		qsort(colors + b->start, b->end - b->start, sizeof(Color), cmpColor);
		u64 total = 0, acc = 0;
		for (int i = b->start; i < b->end; i++)
			total += colors[i].n;
		int split = b->start + 1;
		for (int i = b->start; i < b->end - 1; i++)
		{
			acc += colors[i].n;
			split = i + 1;
			if (acc * 2 >= total)
				break;
		}
		boxes[boxCount] = (Box){split, b->end, 0, 0};
		b->end = split;
		boxStats(b);
		boxStats(&boxes[boxCount]);
		boxCount++;
	}

	palCount = boxCount;
	for (int i = 0; i < boxCount; i++)
	{
		u64 sum[3] = {0, 0, 0}, total = 0;
		for (int j = boxes[i].start; j < boxes[i].end; j++)
		{
			for (int k = 0; k < 3; k++)
				sum[k] += (u64)colors[j].c[k] * colors[j].n;
			total += colors[j].n;
		}
		for (int k = 0; k < 3; k++)
			pal[i][k] = total ? (sum[k] + total / 2) / total : 0;
	}

	for (int round = 0; round < KMEANS_ROUNDS; round++)
	{
		static u64 sums[PAL_SIZE][4];
		memset(sums, 0, sizeof(sums));
		for (int j = 0; j < n; j++)
		{
			int i = nearest(colors[j].c);
			for (int k = 0; k < 3; k++)
				sums[i][k] += (u64)colors[j].c[k] * colors[j].n;
			sums[i][3] += colors[j].n;
		}
		for (int i = 0; i < palCount; i++)
			if (sums[i][3])
				for (int k = 0; k < 3; k++)
					pal[i][k] = (sums[i][k] + sums[i][3] / 2) / sums[i][3];
	}

	for (int j = 0; j < n; j++)
		lut[colors[j].key] = nearest(colors[j].c);
}

static void mapFrame(u8 *dst, const GifFrame *f, int w, int h)
{
	for (int y = 0; y < h; y++)
	{
		const u16 *row = f->img + (f->y + y) * f->stride + f->x;
		for (int x = 0; x < w; x++)
			*dst++ = lut[f->lut ? f->lut[row[x]] : row[x]];
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
		buildPalette(frames, count, w, h);
		fwrite("GIF89a", 1, 6, f);
		put16(f, w);
		put16(f, h);
		fputc(0xF7, f); // global color table, 256 entries
		fputc(0, f);
		fputc(0, f);
		for (int i = 0; i < 256; i++)
			for (int k = 0; k < 3; k++)
				fputc(i < palCount ? pal[i][k] : 0, f);
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
			mapFrame(pix, &frames[i], w, h);
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
