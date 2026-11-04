#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jpeg.h"
#include "stereo.h"

#define QUALITY 90

static u8 *readAll(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	u8 *buf = malloc(*size);
	if (buf && fread(buf, 1, *size, f) != (size_t)*size)
	{
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

static void put32(u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static void put16(u8 *p, u32 v)
{
	p[0] = v >> 8;
	p[1] = v;
}
static void tag(u8 *p, int id, int type, u32 count, u32 value)
{
	put16(p, id);
	put16(p + 2, type);
	put32(p + 4, count);
	put32(p + 8, value);
}

// Where the APP2 segment goes: after SOI and the JFIF APP0.
static long insertAt(const u8 *jpg, long size)
{
	long at = 2;
	if (size > 6 && jpg[2] == 0xFF && jpg[3] == 0xE0)
		at += 2 + (jpg[4] << 8 | jpg[5]);
	return at;
}

#define APP2_FIRST (4 + 4 + 82)  // marker and length, "MPF\0", header + index IFD + 2 entries
#define APP2_SECOND (4 + 4 + 38) // the same, with an attribute IFD of two tags

bool mpoWrite(const char *path, const u16 *left, int lx, int ly, const u16 *right, int rx, int ry, int stride, int w, int h)
{
	char tl[80], tr[80];
	snprintf(tl, sizeof(tl), "%.60s.l.tmp", path);
	snprintf(tr, sizeof(tr), "%.60s.r.tmp", path);
	long sl = 0, sr = 0;
	u8 *jl = NULL, *jr = NULL;
	bool ok = jpegWrite(tl, left + ly * stride + lx, stride, w, h, QUALITY) &&
	          jpegWrite(tr, right + ry * stride + rx, stride, w, h, QUALITY) && (jl = readAll(tl, &sl)) &&
	          (jr = readAll(tr, &sr));
	remove(tl);
	remove(tr);
	if (ok)
	{
		long size1 = sl + APP2_FIRST, size2 = sr + APP2_SECOND;
		long at1 = insertAt(jl, sl), at2 = insertAt(jr, sr);
		// First image: the MP index. Offsets count from the "MM" header,
		// which starts 8 bytes into the segment.
		u8 a[APP2_FIRST] = {0xFF, 0xE2}, *m = a + 8;
		put16(a + 2, APP2_FIRST - 2);
		memcpy(a + 4, "MPF", 4);
		memcpy(m, "MM\0\x2A", 4);
		put32(m + 4, 8);
		put16(m + 8, 3);
		tag(m + 10, 0xB000, 7, 4, 0x30313030); // version "0100"
		tag(m + 22, 0xB001, 4, 1, 2);          // two images
		tag(m + 34, 0xB002, 7, 32, 50);        // the entries, below
		put32(m + 46, 0);
		put32(m + 50, 0x20020002); // representative, disparity image
		put32(m + 54, size1);
		put32(m + 58, 0);
		put32(m + 66, 0x00020002);
		put32(m + 70, size2);
		put32(m + 74, size1 - (at1 + 8)); // from the header to the second image
		// Second image: which one of the two it is.
		u8 b[APP2_SECOND] = {0xFF, 0xE2}, *n = b + 8;
		put16(b + 2, APP2_SECOND - 2);
		memcpy(b + 4, "MPF", 4);
		memcpy(n, "MM\0\x2A", 4);
		put32(n + 4, 8);
		put16(n + 8, 2);
		tag(n + 10, 0xB000, 7, 4, 0x30313030);
		tag(n + 22, 0xB101, 4, 1, 2); // individual image number
		put32(n + 34, 0);
		FILE *f = fopen(path, "wb");
		ok = f && fwrite(jl, 1, at1, f) == (size_t)at1 && fwrite(a, 1, sizeof(a), f) == sizeof(a) &&
		     fwrite(jl + at1, 1, sl - at1, f) == (size_t)(sl - at1) && fwrite(jr, 1, at2, f) == (size_t)at2 &&
		     fwrite(b, 1, sizeof(b), f) == sizeof(b) && fwrite(jr + at2, 1, sr - at2, f) == (size_t)(sr - at2);
		if (f && fclose(f) != 0)
			ok = false;
	}
	free(jl);
	free(jr);
	return ok;
}

bool sbsWrite(const char *path, const u16 *left, int lx, int ly, const u16 *right, int rx, int ry, int stride, int w, int h)
{
	u16 *img = malloc(2 * w * h * sizeof(u16));
	if (!img)
		return false;
	for (int y = 0; y < h; y++)
	{
		memcpy(img + y * 2 * w, left + (ly + y) * stride + lx, w * sizeof(u16));
		memcpy(img + y * 2 * w + w, right + (ry + y) * stride + rx, w * sizeof(u16));
	}
	bool ok = jpegWrite(path, img, 2 * w, 2 * w, h, QUALITY);
	free(img);
	return ok;
}
