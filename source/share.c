#include <3ds.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include "filter.h"
#include "jpeg.h"
#include "share.h"
#include "storage.h"

#define SOC_BUFFER_SIZE 0x100000
#define TIMEOUT_MS 3000
#define MAX_GIFS 1000

static u32 *socBuffer;
static int server = -1;

bool shareStart(char *url, int size)
{
	socBuffer = memalign(0x1000, SOC_BUFFER_SIZE);
	if (!socBuffer || R_FAILED(socInit(socBuffer, SOC_BUFFER_SIZE)))
	{
		free(socBuffer);
		socBuffer = NULL;
		return false;
	}

	struct in_addr ip = {(in_addr_t)gethostid()};
	server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SHARE_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (!ip.s_addr || server < 0 || bind(server, (struct sockaddr *)&addr, sizeof(addr)) || listen(server, 4))
	{
		shareStop();
		return false;
	}
	fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);
	snprintf(url, size, "http://%s:%d/", inet_ntoa(ip), SHARE_PORT);
	return true;
}

void shareStop(void)
{
	if (server >= 0)
		close(server);
	server = -1;
	if (socBuffer)
	{
		socExit();
		free(socBuffer);
		socBuffer = NULL;
	}
}

static bool waitFor(int fd, short events)
{
	struct pollfd p = {fd, events, 0};
	return poll(&p, 1, TIMEOUT_MS) > 0 && (p.revents & events);
}

static bool sendAll(int fd, const void *data, size_t len)
{
	const char *p = data;
	while (len)
	{
		if (!waitFor(fd, POLLOUT))
			return false;
		ssize_t n = send(fd, p, len, 0);
		if (n <= 0)
			return false;
		p += n;
		len -= n;
	}
	return true;
}

static void sendText(int fd, const char *status, const char *type, const char *body)
{
	char head[160];
	int n = snprintf(head, sizeof(head), "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
	                 status, type, (int)strlen(body));
	if (sendAll(fd, head, n))
		sendAll(fd, body, strlen(body));
}

static int cmpDesc(const void *a, const void *b) { return *(const int *)b - *(const int *)a; }

static int listGifs(int *nums, int max)
{
	DIR *dir = opendir(DIR_GIF);
	if (!dir)
		return 0;
	int count = 0;
	struct dirent *e;
	while ((e = readdir(dir)) && count < max)
	{
		int num;
		char ext[8];
		// num * 2, + 1 for a photo; sorted that way, newest comes first.
		if (sscanf(e->d_name, "WIG_%d.%7s", &num, ext) == 2 && (!strcmp(ext, "gif") || !strcmp(ext, "jpg")))
			nums[count++] = num * 2 + (ext[0] == 'j');
	}
	closedir(dir);
	qsort(nums, count, sizeof(int), cmpDesc);
	return count;
}

// The page: colors as CSS variables (the only printf part), then style,
// and a script for the big view. Pictures come as small JPGs first; a tap
// shows the full GIF (or the MP4, which plays smoother) with save buttons.
static const char pageColors[] = "<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Wiggle3DS</title>"
	"<style>:root{--bg:#%06lx;--card:#%06lx;--dim:#%06lx;--a0:#%06lx;--a1:#%06lx}";

static const char pageStyle[] =
	"body{margin:0;background:var(--bg);color:#f8f5ff;font:16px -apple-system,system-ui,sans-serif}"
	"header{padding:24px 16px 0}"
	"h1{margin:0;font-size:30px;display:inline-block;background:linear-gradient(90deg,var(--a0),var(--a1));"
	"-webkit-background-clip:text;background-clip:text;color:transparent}"
	"p{color:var(--dim);margin:6px 0 0;font-size:14px}"
	"h2{font-size:15px;color:var(--dim);margin:20px 16px 0;font-weight:600}"
	".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(96px,1fr));gap:8px;padding:10px 16px}"
	"figure{margin:0;background:var(--card);border-radius:12px;overflow:hidden;position:relative;cursor:pointer;aspect-ratio:4/3}"
	"figure img{display:block;width:100%;height:100%;object-fit:cover}"
	"figure i{position:absolute;left:6px;bottom:6px;font:600 11px system-ui;font-style:normal;background:rgba(0,0,0,.55);"
	"padding:2px 7px;border-radius:99px}"
	"figure b{display:none;position:absolute;top:6px;right:6px;width:22px;height:22px;border-radius:50%;"
	"background:linear-gradient(90deg,var(--a0),var(--a1));text-align:center;line-height:22px;font-size:13px}"
	"figure.saved b{display:block}figure.saved img{opacity:.6}"
	"#box{position:fixed;inset:0;background:rgba(0,0,0,.9);display:flex;flex-direction:column;align-items:center;"
	"justify-content:center;gap:14px;padding:16px}#box[hidden]{display:none}"
	"#media{width:100%}#media img,#media video{display:block;width:100%;max-height:62vh;object-fit:contain;border-radius:14px}"
	"#btns{display:flex;flex-wrap:wrap;gap:8px;justify-content:center}"
	"#btns a{color:#fff;background:linear-gradient(90deg,var(--a0),var(--a1));padding:10px 18px;border-radius:99px;"
	"text-decoration:none;font-weight:600}"
	"#x{position:absolute;top:12px;right:16px;font-size:30px;color:#fff;background:none;border:0}"
	".empty{padding:32px 16px;color:var(--dim)}"
	"</style></head><body>";

static const char pageScript[] =
	"<div id=\"box\" hidden><button id=\"x\">\xc3\x97</button><div id=\"media\"></div><div id=\"btns\"></div>"
	"<p id=\"cap\"></p></div><script>"
	"const saved=new Set(JSON.parse(localStorage.getItem('w3saved')||'[]'));"
	"const all=document.querySelectorAll('figure');"
	"function mark(){all.forEach(f=>f.classList.toggle('saved',saved.has(f.dataset.n)))}mark();"
	"const box=document.getElementById('box'),media=document.getElementById('media'),btns=document.getElementById('btns');"
	"const file=(n,t)=>'/gif/WIG_'+n.padStart(4,'0')+t;"
	"all.forEach(f=>f.onclick=()=>{const d=f.dataset,n=d.n,main=file(n,'.'+d.x);"
	"media.innerHTML=d.v?'<video src=\"'+file(n,'.mp4')+'\" autoplay loop muted playsinline></video>':'<img src=\"'+main+'\">';"
	"let b='<a href=\"'+main+'?download\" download>Save '+d.x.toUpperCase()+'</a>';"
	"if(d.v)b+='<a href=\"'+file(n,'.mp4')+'?download\" download>Save MP4</a>';"
	"if(d.m)b+='<a href=\"'+file(n,'.mpo')+'?download\" download>MPO</a>';"
	"if(d.s)b+='<a href=\"'+file(n,'_LR.jpg')+'?download\" download>3D side by side</a>';"
	"btns.innerHTML=b;btns.querySelectorAll('a').forEach(a=>a.onclick=()=>{saved.add(n);"
	"localStorage.setItem('w3saved',JSON.stringify([...saved]));mark()});"
	"document.getElementById('cap').textContent=d.v?'WIG_'+n.padStart(4,'0'):'Or press and hold the picture, then Save to Photos';"
	"box.hidden=false});"
	"box.onclick=e=>{if(e.target==box||e.target.id=='x'){box.hidden=true;media.innerHTML=''}};"
	"</script></body></html>";

static unsigned long colBg = 0x140b24, colCard = 0x241a3a, colDim = 0xa094be, colAcc0 = 0x7c4dff, colAcc1 = 0xd66eff;

void shareSetColors(u32 bg, u32 card, u32 dim, u32 acc0, u32 acc1)
{
	colBg = bg;
	colCard = card;
	colDim = dim;
	colAcc0 = acc0;
	colAcc1 = acc1;
}

// Shots picked in the gallery: only those are on the page; none: all.
static int only[MAX_GIFS], onlyCount;

void shareSetOnly(const int *nums, int count)
{
	onlyCount = count < MAX_GIFS ? count : MAX_GIFS;
	memcpy(only, nums, onlyCount * sizeof(int));
}

static bool listed(int num)
{
	if (!onlyCount)
		return true;
	for (int i = 0; i < onlyCount; i++)
		if (only[i] == num)
			return true;
	return false;
}

// "Today", "Yesterday" or the date of a shot, for the page's headings.
static void dayLabel(int num, char *out, size_t size)
{
	time_t when = (time_t)storageShotTime(num), now = time(NULL);
	long days = (long)(now / 86400) - (long)(when / 86400);
	if (!when)
		snprintf(out, size, "Earlier");
	else if (days == 0)
		snprintf(out, size, "Today");
	else if (days == 1)
		snprintf(out, size, "Yesterday");
	else
		strftime(out, size, "%A, %-d %B %Y", gmtime(&when));
}

static bool exists(const char *path) { return !access(path, F_OK); }

static void sendPage(int fd)
{
	static int nums[MAX_GIFS];
	int count = listGifs(nums, MAX_GIFS);
	size_t cap = sizeof(pageColors) + sizeof(pageStyle) + sizeof(pageScript) + 512 + count * 260;
	char *body = malloc(cap);
	if (!body)
		return;
	size_t len = snprintf(body, cap, pageColors, colBg, colCard, colDim, colAcc0, colAcc1);
	len += snprintf(body + len, cap - len, "%s<header><h1>Wiggle3DS</h1><p>%s</p></header>", pageStyle,
	                onlyCount ? "The shots you picked. Tap one to save it." : "Tap a picture to see it big and save it. Newest first.");
	char day[48] = "", prev[48] = "";
	int shown = 0;
	for (int i = 0; i < count; i++)
	{
		int num = nums[i] / 2;
		if (!listed(num))
			continue;
		// A heading for each new day, newest first.
		dayLabel(num, day, sizeof(day));
		if (strcmp(day, prev))
		{
			len += snprintf(body + len, cap - len, "%s<h2>%s</h2><div class=\"grid\">", shown ? "</div>" : "", day);
			strcpy(prev, day);
		}
		char path[64];
		bool extra[EXTRA_COUNT];
		for (int k = 0; k < EXTRA_COUNT; k++)
		{
			storageExtraPath(num, k, path, sizeof(path));
			extra[k] = exists(path);
		}
		len += snprintf(body + len, cap - len,
		                "<figure data-n=\"%d\" data-x=\"%s\"%s%s%s><img src=\"/thumb/WIG_%04d.jpg\" loading=\"lazy\" alt=\"\">"
		                "<i>%s</i><b>\xe2\x9c\x93</b></figure>",
		                num, nums[i] & 1 ? "jpg" : "gif", extra[EXTRA_MP4] ? " data-v=\"1\"" : "", extra[EXTRA_MPO] ? " data-m=\"1\"" : "",
		                extra[EXTRA_SBS] ? " data-s=\"1\"" : "", num, nums[i] & 1 ? "Photo" : extra[EXTRA_MP4] ? "GIF · MP4" : "GIF");
		shown++;
	}
	if (shown)
		len += snprintf(body + len, cap - len, "</div>");
	else
		len += snprintf(body + len, cap - len, "<div class=\"empty\">Nothing here yet. Take a shot in Wiggle3DS first.</div>");
	snprintf(body + len, cap - len, "%s", pageScript);
	sendText(fd, "200 OK", "text/html; charset=utf-8", body);
	free(body);
}

// The small picture of a shot for the page: its preview with its filter, as
// a JPG in Cache/, made again when the shot was saved since.
static bool makeThumb(int num, const char *path)
{
	if (exists(path) && storageFileTime(path) >= storageShotTime(num))
		return true;
	ShotInfo info;
	static u16 tiny[THUMB_W * THUMB_H];
	u16 *l = malloc(PREVIEW_W * PREVIEW_H * 2), *r = malloc(PREVIEW_W * PREVIEW_H * 2), *out = malloc(PREVIEW_W * PREVIEW_H * 2);
	bool ok = l && r && out && storageLoadThumb(num, tiny, &info) && storageLoadPreview(num, l, r);
	if (ok)
	{
		int filter = SHOT_FILTER(&info) < FILTER_COUNT ? SHOT_FILTER(&info) : 0;
		filterApply(out, l, PREVIEW_W, 0, 0, PREVIEW_W, PREVIEW_H, filter, 0, SHOT_STRENGTH(&info));
		ok = jpegWrite(path, out, PREVIEW_W, PREVIEW_W, PREVIEW_H, 80);
	}
	free(l);
	free(r);
	free(out);
	return ok;
}

static bool sendPath(int fd, const char *path, const char *type, const char *download);

// tail: the end of the file name after the number, like ".gif" or "_LR.jpg".
static bool sendFile(int fd, int num, const char *tail, bool download)
{
	static const struct { const char *tail, *type; int extra; } kinds[] = {
		{".gif", "image/gif", -1}, {".jpg", "image/jpeg", -2}, {".mp4", "video/mp4", EXTRA_MP4},
		{".mpo", "image/mpo", EXTRA_MPO}, {"_LR.jpg", "image/jpeg", EXTRA_SBS},
	};
	int k = 0;
	while (k < 5 && strcmp(kinds[k].tail, tail))
		k++;
	char path[64];
	if (k == 5)
		path[0] = 0;
	else if (kinds[k].extra == -1)
		storageGifPath(num, path, sizeof(path));
	else if (kinds[k].extra == -2)
		storagePhotoPath(num, path, sizeof(path));
	else
		storageExtraPath(num, kinds[k].extra, path, sizeof(path));
	char name[32];
	snprintf(name, sizeof(name), "WIG_%04d%s", num, tail);
	return sendPath(fd, path, k < 5 ? kinds[k].type : "", download ? name : NULL);
}

// A file from the SD card; `download`: its name, to save instead of show.
static bool sendPath(int fd, const char *path, const char *type, const char *download)
{
	FILE *f = path[0] ? fopen(path, "rb") : NULL;
	if (!f)
	{
		sendText(fd, "404 Not Found", "text/plain", "Not found");
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char disposition[80] = "";
	if (download)
		snprintf(disposition, sizeof(disposition), "Content-Disposition: attachment; filename=\"%s\"\r\n", download);
	char head[256];
	int n = snprintf(head, sizeof(head), "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n%s"
	                 "Cache-Control: no-cache\r\nConnection: close\r\n\r\n", type, size, disposition);
	bool ok = sendAll(fd, head, n);
	static char chunk[32 * 1024];
	size_t got;
	while (ok && (got = fread(chunk, 1, sizeof(chunk), f)) > 0)
		ok = sendAll(fd, chunk, got);
	fclose(f);
	return ok;
}

// One waiting request: the page, a small picture, or a file. True when a
// whole GIF, photo, video or 3D file went out.
static bool answer(int client)
{
	fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) & ~O_NONBLOCK);

	// Only the request line matters.
	char req[1024] = "";
	int len = 0;
	while (len < (int)sizeof(req) - 1 && !strstr(req, "\r\n") && waitFor(client, POLLIN))
	{
		int n = recv(client, req + len, sizeof(req) - 1 - len, 0);
		if (n <= 0)
			break;
		len += n;
		req[len] = 0;
	}
	req[len] = 0;

	bool sent = false;
	int num;
	char tail[12] = "", rest[16] = "";
	if (!strncmp(req, "GET / ", 6) || !strncmp(req, "GET /index.html ", 16))
		sendPage(client);
	else if (sscanf(req, "GET /gif/WIG_%d%11[._LRa-z0-9]%15s", &num, tail, rest) >= 2 && num > 0)
		sent = sendFile(client, num, tail, !strncmp(rest, "?download", 9));
	else if (sscanf(req, "GET /thumb/WIG_%d.jpg", &num) == 1 && num > 0)
	{
		char path[64];
		storageCachePath(num, path, sizeof(path));
		if (!makeThumb(num, path))
			path[0] = 0;
		sendPath(client, path, "image/jpeg", NULL);
	}
	else
		sendText(client, "404 Not Found", "text/plain", "Not found");

	// Let the last bytes go out before closing.
	shutdown(client, SHUT_WR);
	close(client);
	return sent;
}

bool sharePoll(void)
{
	// A few at a time: the page asks for many small pictures at once.
	bool sent = false;
	for (int i = 0; i < 4 && server >= 0; i++)
	{
		int client = accept(server, NULL, NULL);
		if (client < 0)
			break;
		fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) & ~O_NONBLOCK);
		sent |= answer(client);
	}
	return sent;
}
