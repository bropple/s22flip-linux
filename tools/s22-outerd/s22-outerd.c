// SPDX-License-Identifier: GPL-2.0-only
/*
 * s22-outerd: a status screen on the Cat S22 Flip's outer 128x128 display.
 *
 * With the lid closed, the screen lights for SHOW_MS (default 10 s) when the
 * lid closes, when a side, volume or power key is pressed, and when a
 * notification arrives. It shows the time and date, battery, WiFi and
 * Bluetooth, and the newest notification, over a wallpaper. Opening the lid
 * turns it off (backlight off, panel blanked).
 *
 * Wallpaper: a binary PPM (P6) of any size, scaled to cover the screen, at
 * WALLPAPER (default /var/lib/s22-outer/wallpaper.ppm). It is re-read when
 * its modification time changes, and on SIGHUP. Without one the background
 * is a dark gradient.
 *
 * Settings: SETTINGS (default /var/lib/s22-outer/settings), "key=value"
 * lines, re-read whenever the file changes:
 *   clock=12|24      12-hour clock with AM/PM (default), or 24-hour
 *   blink=yes|no     the colon blinks once a second (default yes)
 *   timeout=SECONDS  how long the screen stays lit (default: -t, 10 s)
 *
 * Notifications: one file per notification in NOTIFYDIR (default
 * /run/s22-notify, world-writable and sticky); the newest is shown on one
 * line, as a ticker that scrolls when it does not fit. s22-notify writes and
 * clears them.
 *
 *   s22-outerd [-v] [-f FBNAME] [-B BACKLIGHT] [-t SHOW_MS] [-w WALLPAPER]
 *              [-n NOTIFYDIR] [-s SETTINGS]
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fonts.h"

#define W	128
#define H	128
#define BIT(arr, b)	((arr)[(b) / 8] & (1 << ((b) % 8)))
#define RGB(r, g, b)	((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))

#define WHITE	RGB(255, 255, 255)
#define GREY	RGB(110, 110, 110)
#define BLACK	RGB(0, 0, 0)
#define GREEN	RGB(80, 220, 100)
#define RED	RGB(240, 70, 60)
#define YELLOW	RGB(250, 210, 60)
#define BLUE	RGB(90, 160, 255)

static const char *fb_name = "panel-mipi-dbid";
static const char *bl_name = "ext-backlight";
static int show_ms = 10000;
static const char *wallpaper = "/var/lib/s22-outer/wallpaper.ppm";
static const char *notifydir = "/run/s22-notify";
static const char *settings = "/var/lib/s22-outer/settings";
static bool verbose;

/* From the settings file */
static bool clock12 = true, blink = true;
static int timeout_ms;			/* 0: use show_ms */
static time_t settings_mtime = -1;

#define dbg(...) do { if (verbose) fprintf(stderr, __VA_ARGS__); } while (0)

static uint16_t frame[W * H], wall[W * H];
static bool have_wall;
static time_t wall_mtime;
static char fbsys[300];
static int fbfd = -1;

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0 || write(fd, val, strlen(val)) < 0)
		fprintf(stderr, "s22-outerd: %s: %s\n", path, strerror(errno));
	if (fd >= 0)
		close(fd);
}

/* First line of a small sysfs/proc file, newline stripped; "" if missing */
static char *get(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "re");

	buf[0] = 0;
	if (f) {
		if (!fgets(buf, len, f))
			buf[0] = 0;
		fclose(f);
	}
	buf[strcspn(buf, "\n")] = 0;
	return buf;
}

/* --------------------------------------------------------- the panel --- */
static int find_fb(void)
{
	char p[300], name[64];
	struct dirent *de;
	DIR *d = opendir("/sys/class/graphics");

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "fb", 2) || !de->d_name[2])
			continue;
		snprintf(p, sizeof(p), "/sys/class/graphics/%s/name", de->d_name);
		if (strcmp(get(p, name, sizeof(name)), fb_name))
			continue;
		snprintf(fbsys, sizeof(fbsys), "/sys/class/graphics/%s", de->d_name);
		snprintf(p, sizeof(p), "/dev/%s", de->d_name);
		closedir(d);
		fbfd = open(p, O_WRONLY | O_CLOEXEC);
		return fbfd;
	}
	closedir(d);
	return -1;
}

static bool lit;
static long long lit_since;	/* when the screen last lit: ticker start */
static bool ticking;		/* a ticker is scrolling: redraw often */

static void flush(void)
{
	if (pwrite(fbfd, frame, sizeof(frame), 0) < 0)
		fprintf(stderr, "s22-outerd: write: %s\n", strerror(errno));
}

static void backlight(bool on)
{
	char p[128];

	snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", bl_name);
	put(p, on ? "1" : "0");
}

static void panel_on(void)
{
	char p[320];

	snprintf(p, sizeof(p), "%s/blank", fbsys);
	put(p, "0");
	lit = true;
	lit_since = now_ms();
}

static void panel_off(void)
{
	char p[320];

	backlight(false);
	snprintf(p, sizeof(p), "%s/blank", fbsys);
	put(p, "4");		/* FB_BLANK_POWERDOWN */
	lit = false;
}

/* --------------------------------------------------------- wallpaper --- */
static int ppm_token(FILE *f)
{
	int c, v = 0;

	do {
		c = fgetc(f);
		if (c == '#')
			while (c != '\n' && c != EOF)
				c = fgetc(f);
	} while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
	if (c < '0' || c > '9')
		return -1;
	while (c >= '0' && c <= '9') {
		v = v * 10 + (c - '0');
		c = fgetc(f);
	}
	return v;		/* the single whitespace after it is consumed */
}

static void load_wallpaper(void)
{
	int w, h, maxval, x, y;
	unsigned char *px;
	struct stat st;
	double s, ox, oy;
	FILE *f;

	have_wall = false;
	if (stat(wallpaper, &st))
		return;
	wall_mtime = st.st_mtime;
	f = fopen(wallpaper, "rbe");
	if (!f)
		return;
	if (fgetc(f) != 'P' || fgetc(f) != '6' ||
	    (w = ppm_token(f)) <= 0 || (h = ppm_token(f)) <= 0 ||
	    (maxval = ppm_token(f)) <= 0 || maxval > 255 || w > 4096 || h > 4096) {
		fprintf(stderr, "s22-outerd: %s: not a binary PPM\n", wallpaper);
		fclose(f);
		return;
	}
	px = malloc((size_t)w * h * 3);
	if (!px || fread(px, 3, (size_t)w * h, f) != (size_t)w * h) {
		fprintf(stderr, "s22-outerd: %s: short read\n", wallpaper);
		free(px);
		fclose(f);
		return;
	}
	fclose(f);
	/* Scale to cover, centred */
	s = (double)w / W < (double)h / H ? (double)w / W : (double)h / H;
	ox = (w - W * s) / 2;
	oy = (h - H * s) / 2;
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			int sx = (int)(ox + (x + 0.5) * s), sy = (int)(oy + (y + 0.5) * s);
			unsigned char *p = px + ((size_t)sy * w + sx) * 3;

			wall[y * W + x] = RGB(p[0] * 255 / maxval, p[1] * 255 / maxval,
					      p[2] * 255 / maxval);
		}
	free(px);
	have_wall = true;
	dbg("wallpaper %s: %dx%d\n", wallpaper, w, h);
}

static void background(void)
{
	struct stat st;
	int y;

	if (stat(wallpaper, &st) == 0 ? st.st_mtime != wall_mtime || !have_wall
				      : have_wall)
		load_wallpaper();
	if (have_wall) {
		memcpy(frame, wall, sizeof(frame));
		return;
	}
	for (y = 0; y < H; y++) {	/* dark blue to black */
		uint16_t c = RGB(12 * (H - y) / H, 18 * (H - y) / H, 40 * (H - y) / H);
		int x;

		for (x = 0; x < W; x++)
			frame[y * W + x] = c;
	}
}

/* ---------------------------------------------------------- settings --- */
static bool yes(const char *v)
{
	return !strcmp(v, "yes") || !strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "true");
}

/* Re-read the settings file if it changed (or appeared, or went away) */
static void load_settings(void)
{
	struct stat st;
	char line[128];
	FILE *f;

	if (stat(settings, &st))
		st.st_mtime = 0;
	if (st.st_mtime == settings_mtime)
		return;
	settings_mtime = st.st_mtime;
	clock12 = true;
	blink = true;
	timeout_ms = 0;
	f = fopen(settings, "re");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *k = line, *v = strchr(line, '=');

		while (*k == ' ' || *k == '\t')
			k++;
		if (*k == '#' || !v)
			continue;
		*v++ = 0;
		k[strcspn(k, " \t")] = 0;
		while (*v == ' ' || *v == '\t')
			v++;
		v[strcspn(v, " \t\r\n#")] = 0;
		if (!strcmp(k, "clock"))
			clock12 = atoi(v) != 24;
		else if (!strcmp(k, "blink"))
			blink = yes(v);
		else if (!strcmp(k, "timeout") && atoi(v) > 0)
			timeout_ms = atoi(v) * 1000;
	}
	fclose(f);
	dbg("settings: clock %d, blink %d, timeout %d ms\n", clock12 ? 12 : 24,
	    blink, timeout_ms);
}

static int lit_ms(void)
{
	load_settings();
	return timeout_ms ? timeout_ms : show_ms;
}

/* ----------------------------------------------------------- drawing --- */
static int clip_r = W;		/* pixels at x >= clip_r are not drawn */

static void pixel(int x, int y, uint16_t c)
{
	if (x >= 0 && x < clip_r && y >= 0 && y < H)
		frame[y * W + x] = c;
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
	int i, j;

	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			pixel(i, j, c);
}

/* Halve the brightness of an area, so text over a wallpaper stays legible */
static void dim(int x, int y, int w, int h)
{
	int i, j;

	for (j = y; j < y + h && j < H; j++)
		for (i = x; i < x + w && i < W; i++)
			if (i >= 0 && j >= 0)
				frame[j * W + i] = (frame[j * W + i] >> 1) & 0x7bef;
}

/* 8x16 glyph, each pixel drawn as a k x k block */
static void glyph8(int x, int y, unsigned char ch, int k, uint16_t c)
{
	int r, b;

	for (r = 0; r < 16; r++)
		for (b = 0; b < 8; b++)
			if (font8x16[ch * 16 + r] & (0x80 >> b))
				rect(x + b * k, y + r * k, k, k, c);
}

/* 8x16 text with a one-pixel shadow */
static int text8(int x, int y, const char *s, uint16_t c)
{
	const char *p;

	for (p = s; *p; p++)
		glyph8(x + 1 + (p - s) * 8, y + 1, (unsigned char)*p, 1, BLACK);
	for (p = s; *p; p++, x += 8)
		glyph8(x, y, (unsigned char)*p, 1, c);
	return x;
}

static const char *bt_rune[] = {
	"...#...", "...##..", "#..#.#.", ".#.#..#", "..##.#.", "...##..",
	"..##.#.", ".#.#..#", "#..#.#.", "...##..", "...#...",
};

/* ------------------------------------------------------------ status --- */
static int battery(bool *charging)
{
	char p[300], buf[32];
	struct dirent *de;
	DIR *d = opendir("/sys/class/power_supply");
	int cap = -1;

	*charging = false;
	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", de->d_name);
		get(p, buf, sizeof(buf));
		if (!strcmp(buf, "Battery")) {
			snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", de->d_name);
			if (*get(p, buf, sizeof(buf)))
				cap = atoi(buf);
			snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", de->d_name);
			if (!strcmp(get(p, buf, sizeof(buf)), "Charging"))
				*charging = true;
		} else {
			snprintf(p, sizeof(p), "/sys/class/power_supply/%s/online", de->d_name);
			if (!strcmp(get(p, buf, sizeof(buf)), "1"))
				*charging = true;	/* plugged in */
		}
	}
	closedir(d);
	return cap;
}

/* WiFi bars 0-4, or -1 when not associated */
static int wifi_bars(void)
{
	char line[256];
	FILE *f = fopen("/proc/net/wireless", "re");
	int bars = -1;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char ifname[32];
		float link, level;

		if (sscanf(line, " %31[^:]: %*x %f %f", ifname, &link, &level) != 3 ||
		    strncmp(ifname, "wlan", 4))
			continue;
		if (level >= 0)
			continue;	/* no dBm: not associated */
		bars = level >= -55 ? 4 : level >= -65 ? 3 : level >= -75 ? 2 :
		       level >= -85 ? 1 : 0;
	}
	fclose(f);
	return bars;
}

/* A Bluetooth ACL link shows up as hciN:HANDLE */
static bool bt_connected(void)
{
	struct dirent *de;
	DIR *d = opendir("/sys/class/bluetooth");
	bool on = false;

	if (!d)
		return false;
	while ((de = readdir(d)))
		if (!strncmp(de->d_name, "hci", 3) && strchr(de->d_name, ':'))
			on = true;
	closedir(d);
	return on;
}

/* Newest notification: its text and how many there are */
static int notification(char *text, size_t len)
{
	char p[512];
	struct dirent *de;
	struct stat st;
	time_t newest = 0;
	DIR *d = opendir(notifydir);
	int n = 0;

	text[0] = 0;
	if (!d)
		return 0;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "%s/%s", notifydir, de->d_name);
		if (stat(p, &st) || !S_ISREG(st.st_mode))
			continue;
		n++;
		if (st.st_mtime >= newest) {
			FILE *f = fopen(p, "re");
			size_t r = 0;

			newest = st.st_mtime;
			if (f) {
				r = fread(text, 1, len - 1, f);
				fclose(f);
			}
			text[r] = 0;
		}
	}
	closedir(d);
	for (char *c = text; *c; c++)	/* one line of plain characters */
		if ((unsigned char)*c < ' ')
			*c = ' ';
	for (size_t l = strlen(text); l && text[l - 1] == ' '; l--)
		text[l - 1] = 0;
	return n;
}

/* ------------------------------------------------------------ screen --- */
static void draw(void)
{
	char buf[64], note[512];
	time_t t = time(NULL);
	struct tm tm;
	bool charging;
	int cap, bars, i, n, x, w;

	localtime_r(&t, &tm);
	background();

	/* Status bar */
	dim(0, 0, W, 18);
	bars = wifi_bars();
	if (bars >= 0)
		for (i = 0; i < 4; i++)
			rect(2 + i * 4, 14 - 3 * (i + 1), 3, 3 * (i + 1),
			     i < bars ? WHITE : GREY);
	if (bt_connected())
		for (i = 0; i < 11; i++)
			for (x = 0; x < 7; x++)
				if (bt_rune[i][x] == '#')
					pixel(22 + x, 3 + i, BLUE);
	cap = battery(&charging);
	if (cap >= 0) {
		rect(103, 4, 20, 10, WHITE);		/* outline */
		rect(104, 5, 18, 8, BLACK);
		rect(123, 7, 2, 4, WHITE);		/* nub */
		w = (cap * 18 + 50) / 100;
		rect(104, 5, w, 8, charging ? YELLOW : cap <= 20 ? RED : GREEN);
		snprintf(buf, sizeof(buf), "%d%%", cap);
		text8(101 - (int)strlen(buf) * 8, 1, buf, WHITE);
	}

	/* Clock, the 8x16 font at 2x: 12-hour without a leading zero and a
	 * small AM/PM after it, or 24-hour; the colon blinks once a second */
	load_settings();
	if (clock12) {
		int h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;

		snprintf(buf, sizeof(buf), "%d:%02d", h, tm.tm_min);
		w = (int)strlen(buf) * 16 + 4 + 16;
	} else {
		strftime(buf, sizeof(buf), "%H:%M", &tm);
		w = (int)strlen(buf) * 16;
	}
	x = (W - w) / 2;
	for (i = 0; buf[i]; i++)
		if (buf[i] != ':' || !blink || tm.tm_sec % 2 == 0) {
			glyph8(x + i * 16 + 1, 27, (unsigned char)buf[i], 2, BLACK);
			glyph8(x + i * 16, 26, (unsigned char)buf[i], 2, WHITE);
		}
	/* Same baseline: digits and capitals have ink in rows 2-11 of a cell,
	 * so the 2x clock's last ink row is 26 + 2 * 11 + 1 = 49 = 38 + 11 */
	if (clock12)
		text8(x + (int)strlen(buf) * 16 + 4, 38, tm.tm_hour < 12 ? "AM" : "PM", WHITE);
	strftime(buf, sizeof(buf), "%a %d %b %Y", &tm);
	text8((W - (int)strlen(buf) * 8) / 2, 66, buf, WHITE);

	/* Notification: the newest on one line, scrolling (ticker) when it
	 * does not fit; "+N" at the right when there are others */
	n = notification(note, sizeof(note));
	ticking = false;
	if (n) {
		int right = W, tw = (int)strlen(note) * 8;

		dim(0, 96, W, 20);
		if (n > 1) {
			snprintf(buf, sizeof(buf), "+%d", n - 1);
			right = W - (int)strlen(buf) * 8 - 2;
			text8(right + 1, 98, buf, YELLOW);
		}
		clip_r = right;
		if (tw <= right - 4) {
			text8(2, 98, note, WHITE);
		} else {
			/* 1 s still, then 40 px/s, looping with a 32 px gap */
			long long el = now_ms() - lit_since - 1000;
			int period = tw + 32, off = el > 0 ? (int)(el * 40 / 1000 % period) : 0;

			text8(2 - off, 98, note, WHITE);
			text8(2 - off + period, 98, note, WHITE);
			ticking = true;
		}
		clip_r = W;
	}
	flush();
}

/* ------------------------------------------------------------ inputs --- */
#define MAX_INPUTS 16
static struct { int fd; char path[300]; } inputs[MAX_INPUTS];
static int ninputs;
static bool lid_closed;

static bool wake_key(int code)
{
	return code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN ||
	       code == KEY_POWER || code == KEY_NUMERIC_B;
}

static void scan_inputs(void)
{
	unsigned char keys[KEY_MAX / 8 + 1], sw[SW_MAX / 8 + 1];
	char p[300];
	struct dirent *de;
	DIR *d = opendir("/dev/input");
	int i, fd;

	if (!d)
		return;
	while ((de = readdir(d)) && ninputs < MAX_INPUTS) {
		bool want = false, known = false;

		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
		for (i = 0; i < ninputs; i++)
			if (!strcmp(inputs[i].path, p))
				known = true;
		if (known)
			continue;
		fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(keys, 0, sizeof(keys));
		memset(sw, 0, sizeof(sw));
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		ioctl(fd, EVIOCGBIT(EV_SW, sizeof(sw)), sw);
		if (BIT(sw, SW_LID)) {
			unsigned char state[SW_MAX / 8 + 1] = { 0 };

			ioctl(fd, EVIOCGSW(sizeof(state)), state);
			lid_closed = BIT(state, SW_LID);
			want = true;
		}
		for (i = 0; i <= KEY_MAX && !want; i++)
			if (BIT(keys, i) && wake_key(i))
				want = true;
		if (!want) {
			close(fd);
			continue;
		}
		inputs[ninputs].fd = fd;
		snprintf(inputs[ninputs].path, sizeof(inputs[ninputs].path), "%s", p);
		ninputs++;
		dbg("input %s\n", p);
	}
	closedir(d);
}

static void drop_input(int i)
{
	close(inputs[i].fd);
	inputs[i] = inputs[--ninputs];
}

/* -------------------------------------------------------------- main --- */
static volatile sig_atomic_t quit, reload;
static void on_term(int sig) { (void)sig; quit = 1; }
static void on_hup(int sig) { (void)sig; reload = 1; }

int main(int argc, char **argv)
{
	struct pollfd pfd[MAX_INPUTS + 2];
	long long show_until = 0;
	int opt, i, in_notify, in_dev;

	while ((opt = getopt(argc, argv, "vf:B:t:w:n:s:")) != -1) {
		switch (opt) {
		case 'v': verbose = true; break;
		case 'f': fb_name = optarg; break;
		case 'B': bl_name = optarg; break;
		case 't': show_ms = atoi(optarg); break;
		case 'w': wallpaper = optarg; break;
		case 'n': notifydir = optarg; break;
		case 's': settings = optarg; break;
		default:
			fprintf(stderr, "usage: %s [-v] [-f FBNAME] [-B BACKLIGHT] [-t SHOW_MS] "
				"[-w WALLPAPER] [-n NOTIFYDIR] [-s SETTINGS]\n", argv[0]);
			return 2;
		}
	}
	for (i = 0; i < 40 && find_fb() < 0; i++)
		usleep(250000);
	if (fbfd < 0) {
		fprintf(stderr, "s22-outerd: no framebuffer named %s\n", fb_name);
		return 1;
	}
	mkdir(notifydir, 01777);
	chmod(notifydir, 01777);
	in_notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	inotify_add_watch(in_notify, notifydir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	in_dev = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	inotify_add_watch(in_dev, "/dev/input", IN_CREATE | IN_ATTRIB);
	scan_inputs();
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGHUP, on_hup);
	load_wallpaper();
	fprintf(stderr, "s22-outerd: %s, %d inputs, lid %s\n", fbsys, ninputs,
		lid_closed ? "closed" : "open");

	panel_off();
	load_settings();
	if (lid_closed)
		show_until = now_ms() + lit_ms();

	while (!quit) {
		long long t = now_ms();
		int timeout = -1, n = 0, r;
		bool wake = false, redraw = false;

		/* Show or hide */
		if (show_until > t && lid_closed) {
			if (!lit) {
				panel_on();
				draw();
				backlight(true);
			}
			timeout = (int)(show_until - t);
			/* redraw every second for the clock, 20 times a second for
			 * a scrolling ticker */
			r = ticking ? 50 - (int)(t % 50) : 1000 - (int)(t % 1000);
			if (r < timeout)
				timeout = r;
		} else if (lit) {
			panel_off();
		}

		for (i = 0; i < ninputs; i++)
			pfd[n++] = (struct pollfd){ .fd = inputs[i].fd, .events = POLLIN };
		pfd[n++] = (struct pollfd){ .fd = in_notify, .events = POLLIN };
		pfd[n++] = (struct pollfd){ .fd = in_dev, .events = POLLIN };
		r = poll(pfd, n, timeout);
		if (r < 0 && errno != EINTR)
			break;
		if (reload) {
			reload = 0;
			wall_mtime = 0;
			load_wallpaper();
			redraw = true;
		}
		if (r <= 0) {
			if (lit && show_until > now_ms())
				draw();		/* the clock ticks */
			continue;
		}

		for (i = ninputs - 1; i >= 0; i--) {
			struct input_event ev;
			ssize_t len;

			if (!(pfd[i].revents & (POLLIN | POLLERR | POLLHUP)))
				continue;
			while ((len = read(inputs[i].fd, &ev, sizeof(ev))) == sizeof(ev)) {
				if (ev.type == EV_SW && ev.code == SW_LID) {
					lid_closed = ev.value;
					dbg("lid %s\n", ev.value ? "closed" : "open");
					if (ev.value)
						wake = true;
					else
						show_until = 0;
				} else if (ev.type == EV_KEY && ev.value == 1 &&
					   wake_key(ev.code)) {
					dbg("key %d\n", ev.code);
					wake = true;
				}
			}
			if (len < 0 && errno == ENODEV)
				drop_input(i);
		}
		if (pfd[n - 2].revents & POLLIN) {
			char buf[4096];

			while (read(in_notify, buf, sizeof(buf)) > 0)
				;
			wake = true;	/* a new or cleared notification */
		}
		if (pfd[n - 1].revents & POLLIN) {
			char buf[4096];

			while (read(in_dev, buf, sizeof(buf)) > 0)
				;
			usleep(100000);	/* let udev set permissions */
			scan_inputs();
		}
		if (wake && lid_closed) {
			show_until = now_ms() + lit_ms();
			redraw = lit;
		}
		if (redraw && lit)
			draw();
	}
	panel_off();
	return 0;
}
