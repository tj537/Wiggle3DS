// Tests for the parts that run the same on the computer: filters, auto
// develop, the MP4 and MPO writers. `make test` builds and runs them with
// the computer's compiler, together with tools/preview.c on a made-up
// camera pair, so every screen is drawn at least once.
// usage: test <tmpdir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/camera.h"
#include "../source/develop.h"
#include "../source/filter.h"
#include "../source/mp4.h"
#include "../source/stereo.h"

static int failures;
static const char *dir;

#define CHECK(cond, ...)                                     \
	do                                                       \
	{                                                        \
		if (!(cond))                                         \
		{                                                    \
			failures++;                                      \
			printf("FAILED %s:%d: ", __FILE__, __LINE__);    \
			printf(__VA_ARGS__);                             \
			printf("\n");                                    \
		}                                                    \
	} while (0)

static u16 rgb(int r, int g, int b) { return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3; }

// ---- filters ----

static void testFilters(void)
{
	// Every filter is in the menu exactly once.
	int seen[FILTER_COUNT] = {0};
	for (int g = 0; g < filterGroupCount(); g++)
		for (int i = 0; i < filterGroupSize(g); i++)
		{
			int f = filterGroupItem(g, i);
			CHECK(f >= 0 && f < FILTER_COUNT, "group %s has filter %d", filterGroupName(g), f);
			if (f >= 0 && f < FILTER_COUNT)
				seen[f]++;
		}
	for (int f = 0; f < FILTER_COUNT; f++)
	{
		CHECK(seen[f] == 1, "filter %d (%s) is in the menu %d times", f, filterName(f), seen[f]);
		CHECK(filterName(f) && filterName(f)[0], "filter %d has no name", f);
		for (int g = 0; g < f; g++)
			CHECK(strcmp(filterName(f), filterName(g)), "two filters named %s", filterName(f));
	}
	// Stepping through all of them comes back to the start.
	int f = F_ORIGINAL;
	for (int i = 0; i < FILTER_COUNT; i++)
		f = filterNext(f, 1);
	CHECK(f == F_ORIGINAL, "filterNext doesn't go round");
	// Ids that saved shots use stay where they are.
	CHECK(!strcmp(filterName(F_DREAM), "Dream") && F_DREAM == 4, "Dream moved");
	CHECK(!strcmp(filterName(F_FEEDBACK), "Feedback") && F_FEEDBACK == 67, "Feedback moved");

	// Every filter runs on a small image without going outside it.
	enum { W = 64, H = 48 };
	static u16 img[W * H], out[W * H + 1];
	for (int i = 0; i < W * H; i++)
		img[i] = rgb(i * 7 & 255, i * 3 & 255, i & 255);
	for (int k = 0; k < FILTER_COUNT; k++)
	{
		out[W * H] = 0xBEEF;
		filterApply(out, img, W, 0, 0, W, H, k, 3, 70);
		CHECK(out[W * H] == 0xBEEF, "%s writes past the image", filterName(k));
	}
}

// ---- auto develop ----

static u16 left[CAM_W * CAM_H], right[CAM_W * CAM_H];

// Mean brightness 0..255 on a sparse grid.
static int meterLuma(const u16 *img)
{
	long sum = 0, n = 0;
	for (int i = 0; i < CAM_W * CAM_H; i += 97, n++)
	{
		u16 p = img[i];
		sum += (77 * ((p >> 11) << 3) + 150 * ((p >> 5 & 63) << 2) + 29 * ((p & 31) << 3)) >> 8;
	}
	return (int)(sum / n);
}

static void fill(int r, int g, int b)
{
	for (int y = 0; y < CAM_H; y++)
		for (int x = 0; x < CAM_W; x++)
		{
			int k = (x / 40 + y / 40) % 3; // some texture: three shades
			left[y * CAM_W + x] = right[y * CAM_W + x] = rgb(r * (k + 2) / 3, g * (k + 2) / 3, b * (k + 2) / 3);
		}
}

static void testDevelop(void)
{
	fill(70, 64, 60); // dark, a bit warm
	int before = meterLuma(left);
	autoDevelop(left, right, false);
	int after = meterLuma(left);
	CHECK(after > before + 30 && after > 85 && after < 140, "dark scene: %d -> %d", before, after);
	CHECK(!memcmp(left, right, sizeof(left)), "the eyes were developed differently");

	fill(140, 140, 140); // already fine
	before = meterLuma(left);
	autoDevelop(left, right, false);
	after = meterLuma(left);
	CHECK(abs(after - before) < 14, "well exposed scene changed: %d -> %d", before, after);

	// Double exposure: black under a picture leaves the picture, a bit darker.
	static u16 first[CAM_W * CAM_H];
	fill(200, 200, 200);
	memcpy(first, left, sizeof(first));
	memset(left, 0, sizeof(left));
	doubleExpose(left, first);
	int both = meterLuma(left), alone = meterLuma(first);
	CHECK(both > alone * 3 / 4 && both < alone, "double exposure over black: %d from %d", both, alone);

}

// ---- files ----

static unsigned char *readFile(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc(*size);
	if (fread(b, 1, *size, f) != (size_t)*size)
		*size = 0;
	fclose(f);
	return b;
}

static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// The box of `type` within [from, to), -1 if none.
static long findBox(const unsigned char *b, long from, long to, const char *type)
{
	for (long at = from; at + 8 <= to;)
	{
		long size = be32(b + at);
		if (!memcmp(b + at + 4, type, 4))
			return at;
		if (size < 8)
			return -1;
		at += size;
	}
	return -1;
}

static void testMp4(void)
{
	// Three looks, the first one again at the end: a hard cut back and forth.
	enum { W = 100, H = 60 };
	static u16 a[W * H], b[W * H], c[W * H];
	for (int i = 0; i < W * H; i++)
	{
		a[i] = rgb(i % W * 2, 0, 0);
		b[i] = rgb(0, i / W * 4, 0);
		c[i] = 0; // all black: zeros that need emulation prevention
	}
	GifFrame frames[4] = {{a, W, 0, 0}, {b, W, 0, 0}, {c, W, 0, 0}, {a, W, 0, 0}};
	char path[256];
	snprintf(path, sizeof(path), "%s/test.mp4", dir);
	CHECK(mp4Write(path, frames, 4, W, H, 120), "mp4Write failed");
	long size = 0;
	unsigned char *m = readFile(path, &size);
	CHECK(m && size > 0, "no MP4 written");
	if (!m)
		return;
	long ftyp = findBox(m, 0, size, "ftyp"), mdat = findBox(m, 0, size, "mdat"), moov = findBox(m, 0, size, "moov");
	CHECK(ftyp == 0 && mdat > 0 && moov > 0, "top level boxes: ftyp %ld mdat %ld moov %ld", ftyp, mdat, moov);
	// Three distinct frames stored, each 4 * 7 macroblocks of 384 bytes and a little.
	long data = be32(m + mdat) - 8;
	CHECK(data > 3 * 28 * 384 && data < 4 * 28 * 384, "mdat holds %ld bytes", data);
	long trak = findBox(m, moov + 8, size, "trak"), mdia = findBox(m, trak + 8, size, "mdia");
	long minf = findBox(m, mdia + 8, size, "minf"), stbl = findBox(m, minf + 8, size, "stbl");
	long stsz = stbl > 0 ? findBox(m, stbl + 8, size, "stsz") : -1;
	CHECK(stsz > 0, "no stsz");
	if (stsz > 0)
	{
		unsigned n = be32(m + stsz + 16);
		CHECK(n >= 50 && n % 4 == 0, "%u samples: should loop about six seconds", n);
	}
	// No start code inside a NAL unit.
	for (long i = mdat + 8; i + 2 < mdat + 8 + data; i++)
		if (!m[i] && !m[i + 1] && m[i + 2] <= 2)
		{
			// The 4 byte length prefixes have zeros; skip those.
			long at = mdat + 8;
			bool prefix = false;
			while (at < i + 3 && at < mdat + 8 + data)
			{
				if (i >= at && i < at + 4)
					prefix = true;
				at += 4 + be32(m + at);
			}
			CHECK(prefix, "start code at %ld", i);
			if (!prefix)
				break;
		}
	free(m);

	// With ffmpeg on the computer: does it decode, every frame?
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "command -v ffmpeg >/dev/null && ffmpeg -v error -xerror -i %s -f null - 2>&1", path);
	FILE *p = popen(cmd, "r");
	char line[256] = "";
	bool errors = p && fgets(line, sizeof(line), p);
	if (p)
		pclose(p);
	CHECK(!errors, "ffmpeg: %s", line);
}

static void testMpo(void)
{
	enum { W = 64, H = 48 };
	static u16 l[W * H], r[W * H];
	for (int i = 0; i < W * H; i++)
	{
		l[i] = rgb(200, i % W * 4, 40);
		r[i] = rgb(40, i % W * 4, 200);
	}
	char path[256];
	snprintf(path, sizeof(path), "%s/test.mpo", dir);
	CHECK(mpoWrite(path, l, 0, 0, r, 0, 0, W, W, H), "mpoWrite failed");
	long size = 0;
	unsigned char *m = readFile(path, &size);
	CHECK(m && size > 4 && m[0] == 0xFF && m[1] == 0xD8, "no JPEG at the start");
	if (!m)
		return;
	// The MP index: after JFIF, "MPF", then the entries.
	long app2 = 2 + 2 + (m[4] << 8 | m[5]);
	CHECK(m[app2] == 0xFF && m[app2 + 1] == 0xE2 && !memcmp(m + app2 + 4, "MPF", 4), "no MPF segment");
	const unsigned char *mm = m + app2 + 8, *e = mm + 50;
	unsigned size1 = be32(e + 4), size2 = be32(e + 20), off2 = be32(e + 24);
	long second = (mm - m) + off2;
	CHECK(be32(mm + 4) == 8 && size1 + size2 == (unsigned)size, "sizes %u + %u, file %ld", size1, size2, size);
	CHECK(second == (long)size1 && m[second] == 0xFF && m[second + 1] == 0xD8, "second image not where the index says");
	CHECK(m[size1 - 2] == 0xFF && m[size1 - 1] == 0xD9 && m[size - 2] == 0xFF && m[size - 1] == 0xD9, "images don't end with EOI");
	free(m);

	snprintf(path, sizeof(path), "%s/test_LR.jpg", dir);
	CHECK(sbsWrite(path, l, 0, 0, r, 0, 0, W, W, H), "sbsWrite failed");
}

int main(int argc, char **argv)
{
	dir = argc > 1 ? argv[1] : ".";
	filterInit();
	testFilters();
	testDevelop();
	testMp4();
	testMpo();
	printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
	return failures ? 1 : 0;
}
