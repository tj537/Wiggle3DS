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
		if (sscanf(e->d_name, "WIG_%d.%7s", &num, ext) == 2 && !strcmp(ext, "gif"))
			nums[count++] = num;
	}
	closedir(dir);
	qsort(nums, count, sizeof(int), cmpDesc);
	return count;
}

static const char pageHead[] = // printf format: colors below
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Wiggle3DS</title><style>"
	"body{margin:0;background:#%06lx;color:#f8f5ff;font:16px -apple-system,system-ui,sans-serif}"
	"header{padding:24px 16px 4px}"
	"h1{margin:0;font-size:30px;display:inline-block;background:linear-gradient(90deg,#%06lx,#%06lx);"
	"-webkit-background-clip:text;background-clip:text;color:transparent}"
	"p{color:#%06lx;margin:6px 0 0;font-size:14px}"
	".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:12px;padding:16px}"
	"figure{margin:0;background:#%06lx;border-radius:16px;overflow:hidden}"
	"img{display:block;width:100%%;height:auto}"
	"figcaption{display:flex;justify-content:space-between;align-items:center;padding:8px 10px 8px 12px;font-size:13px;color:#%06lx}"
	"a{color:#fff;background:linear-gradient(90deg,#%06lx,#%06lx);padding:5px 12px;border-radius:99px;text-decoration:none;font-weight:600}"
	".empty{padding:32px 16px;color:#%06lx}"
	"</style></head><body><header><h1>Wiggle3DS</h1>"
	"<p>Press and hold a GIF, then choose \xe2\x80\x9cSave to Photos\xe2\x80\x9d.</p></header>";

static unsigned long colBg = 0x140b24, colCard = 0x241a3a, colDim = 0xa094be, colAcc0 = 0x7c4dff, colAcc1 = 0xd66eff;

void shareSetColors(u32 bg, u32 card, u32 dim, u32 acc0, u32 acc1)
{
	colBg = bg;
	colCard = card;
	colDim = dim;
	colAcc0 = acc0;
	colAcc1 = acc1;
}

static void sendPage(int fd)
{
	static int nums[MAX_GIFS];
	int count = listGifs(nums, MAX_GIFS);
	size_t cap = sizeof(pageHead) + 256 + count * 200;
	char *body = malloc(cap);
	if (!body)
		return;
	// Order of the colors in pageHead.
	size_t len = snprintf(body, cap, pageHead, colBg, colAcc0, colAcc1, colDim, colCard, colDim, colAcc0, colAcc1, colDim);
	if (!count)
		len += snprintf(body + len, cap - len, "<div class=\"empty\">No GIFs yet. Save one in Wiggle3DS first.</div>");
	else
		len += snprintf(body + len, cap - len, "<div class=\"grid\">");
	for (int i = 0; i < count; i++)
		len += snprintf(body + len, cap - len,
		                "<figure><img src=\"/gif/WIG_%04d.gif\" loading=\"lazy\" alt=\"WIG_%04d\">"
		                "<figcaption>WIG_%04d<a href=\"/gif/WIG_%04d.gif?download\" download>Save</a></figcaption></figure>",
		                nums[i], nums[i], nums[i], nums[i]);
	snprintf(body + len, cap - len, "%s</body></html>", count ? "</div>" : "");
	sendText(fd, "200 OK", "text/html; charset=utf-8", body);
	free(body);
}

static bool sendGif(int fd, int num, bool download)
{
	char path[64];
	storageGifPath(num, path, sizeof(path));
	FILE *f = fopen(path, "rb");
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
		snprintf(disposition, sizeof(disposition), "Content-Disposition: attachment; filename=\"WIG_%04d.gif\"\r\n", num);
	char head[256];
	int n = snprintf(head, sizeof(head), "HTTP/1.0 200 OK\r\nContent-Type: image/gif\r\nContent-Length: %ld\r\n%s"
	                 "Cache-Control: no-cache\r\nConnection: close\r\n\r\n", size, disposition);
	bool ok = sendAll(fd, head, n);
	static char chunk[32 * 1024];
	size_t got;
	while (ok && (got = fread(chunk, 1, sizeof(chunk), f)) > 0)
		ok = sendAll(fd, chunk, got);
	fclose(f);
	return ok;
}

bool sharePoll(void)
{
	if (server < 0)
		return false;
	int client = accept(server, NULL, NULL);
	if (client < 0)
		return false;
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
	char rest[16] = "";
	if (!strncmp(req, "GET / ", 6) || !strncmp(req, "GET /index.html ", 16))
		sendPage(client);
	else if (sscanf(req, "GET /gif/WIG_%d.gif%15s", &num, rest) >= 1 && num > 0)
		sent = sendGif(client, num, !strncmp(rest, "?download", 9));
	else
		sendText(client, "404 Not Found", "text/plain", "Not found");

	// Let the last bytes go out before closing.
	shutdown(client, SHUT_WR);
	close(client);
	return sent;
}
