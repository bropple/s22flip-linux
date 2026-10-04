// SPDX-License-Identifier: GPL-2.0-only
/*
 * s22-lidd: the main display follows the Cat S22 Flip's lid. Closed: the
 * main panel's framebuffer is blanked and its backlight powered down; open:
 * both come back. Suspend on lid close is left to later (elogind ignores
 * the lid for now).
 *
 *   s22-lidd [-f FB] [-b BACKLIGHT]   (defaults fb0, backlight)
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BIT(arr, b)	((arr)[(b) / 8] & (1 << ((b) % 8)))

static const char *fb = "fb0", *bl = "backlight";

static void put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0 || write(fd, val, strlen(val)) < 0)
		fprintf(stderr, "s22-lidd: %s: %s\n", path, strerror(errno));
	if (fd >= 0)
		close(fd);
}

static void display(int on)
{
	char p[128];

	snprintf(p, sizeof(p), "/sys/class/graphics/%s/blank", fb);
	put(p, on ? "0" : "4");		/* FB_BLANK_UNBLANK / POWERDOWN */
	snprintf(p, sizeof(p), "/sys/class/backlight/%s/bl_power", bl);
	put(p, on ? "0" : "4");
}

/* The input device that reports SW_LID */
static int open_lid(void)
{
	unsigned char sw[SW_MAX / 8 + 1];
	char p[300];
	struct dirent *de;
	DIR *d = opendir("/dev/input");
	int fd;

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
		fd = open(p, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(sw, 0, sizeof(sw));
		if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(sw)), sw) >= 0 && BIT(sw, SW_LID)) {
			closedir(d);
			return fd;
		}
		close(fd);
	}
	closedir(d);
	errno = ENODEV;
	return -1;
}

int main(int argc, char **argv)
{
	unsigned char state[SW_MAX / 8 + 1] = { 0 };
	struct input_event ev;
	int fd, opt;

	while ((opt = getopt(argc, argv, "f:b:")) != -1) {
		switch (opt) {
		case 'f': fb = optarg; break;
		case 'b': bl = optarg; break;
		default:
			fprintf(stderr, "usage: %s [-f FB] [-b BACKLIGHT]\n", argv[0]);
			return 2;
		}
	}
	fd = open_lid();
	if (fd < 0) {
		perror("s22-lidd: lid switch");
		return 1;
	}
	/* Start from the lid's current position */
	ioctl(fd, EVIOCGSW(sizeof(state)), state);
	display(!BIT(state, SW_LID));
	while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
		if (ev.type == EV_SW && ev.code == SW_LID)
			display(!ev.value);
	perror("s22-lidd: read");
	return 1;
}
