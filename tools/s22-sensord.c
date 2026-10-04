// SPDX-License-Identifier: GPL-2.0-only
/*
 * s22-sensord: sensors of the Cat S22 Flip, which sit behind the ADSP.
 *
 * The sensor bus belongs to the ADSP, whose sensor manager (SMGR, QMI
 * service 256) only starts once the phone side provides a sensor registry
 * (service 0x10f) and a time service (0x118, instance 50). This daemon
 * provides both, then streams the sensors from SMGR and exposes them:
 *
 *   accelerometer  uinput device with INPUT_PROP_ACCELEROMETER, milli-g
 *                  (resolution 1000), Linux/Android axes; and a file
 *   proximity      uinput switch SW_FRONT_PROXIMITY (1 = near); and a file
 *   light, pressure  files only
 *
 * Files under OUTDIR (default /run/s22-sensors) hold the latest reading:
 * "accel" (x y z in m/s2), "proximity" (0/1 and raw count), "light" (lux),
 * "pressure" (hPa).
 *
 *   s22-sensord [-g GROUPS] [-r SNS.REG] [-o OUTDIR] [-a HZ] [-p HZ] [-l HZ] [-b HZ]
 *
 * Rates are report rates in Hz; 0 disables a sensor. Registry data: the
 * phone's own /persist/sensors/sns.reg, and a group table extracted from
 * the phone's own sensors.qti with sns-reg-groups.py. The protocol was
 * worked out from those binaries and live traffic.
 *
 * Build: aarch64-linux-gnu-gcc -static -O2 -o s22-sensord s22-sensord.c
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif
#define QRTR_PORT_CTRL		0xfffffffeu
#define QRTR_TYPE_NEW_SERVER	4
#define QRTR_TYPE_DEL_SERVER	5
#define QRTR_TYPE_NEW_LOOKUP	10

#define QMI_REQUEST		0
#define QMI_RESPONSE		2
#define QMI_INDICATION		4

/* Services this daemon provides to the ADSP */
#define REG_SERVICE		0x10f
#define REG_VERSION		2
#define TIME_SERVICE		0x118
#define TIME_VERSION		2
#define TIME_INSTANCE		50

/* The ADSP's sensor manager */
#define SMGR_SERVICE		0x100
#define SMGR_INSTANCE		50
#define SMGR_REPORT		0x02
#define SMGR_REPORT_IND		0x03

/* SMGR sensor IDs and data types */
#define SENSOR_ACCEL		0x00
#define SENSOR_PRESSURE		0x1e
#define SENSOR_PROX_LIGHT	0x28

#define MAX_GROUPS		512
#define Q16			65536.0
#define STANDARD_GRAVITY	9.80665

struct sockaddr_qrtr {
	unsigned short sq_family;
	uint32_t sq_node;
	uint32_t sq_port;
};

struct group {
	unsigned int id, size, offset;
};

struct stream {
	const char *name;
	uint8_t report_id;
	uint8_t sensor;
	uint8_t data_type;
	unsigned int rate;
};

static struct stream streams[] = {
	{ "accel",	1, SENSOR_ACCEL,	0, 10 },
	{ "proximity",	2, SENSOR_PROX_LIGHT,	0, 5 },
	{ "light",	3, SENSOR_PROX_LIGHT,	1, 2 },
	{ "pressure",	4, SENSOR_PRESSURE,	0, 1 },
};
#define NSTREAMS (sizeof(streams) / sizeof(streams[0]))

static struct group groups[MAX_GROUPS];
static int ngroups;
static uint8_t *reg;
static size_t reglen;
static const char *outdir = "/run/s22-sensors";
static int ui_accel = -1, ui_prox = -1;
static int last_prox = -1;
static uint16_t txn;

static void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ---- QMI helpers ---- */

static const uint8_t *tlv(const uint8_t *p, size_t n, uint8_t type, unsigned int *len)
{
	while (n >= 3) {
		unsigned int l = p[1] | p[2] << 8;

		if (l > n - 3)
			return NULL;
		if (p[0] == type) {
			*len = l;
			return p + 3;
		}
		p += 3 + l;
		n -= 3 + l;
	}
	return NULL;
}

static uint8_t *put_tlv(uint8_t *p, uint8_t type, const void *val, unsigned int len)
{
	p[0] = type;
	p[1] = len & 0xff;
	p[2] = len >> 8;
	memcpy(p + 3, val, len);
	return p + 3 + len;
}

/* The registry interface's result TLV is a bare 16-bit result */
static uint8_t *put_result(uint8_t *p, uint16_t result)
{
	uint8_t v[2] = { result & 0xff, result >> 8 };

	return put_tlv(p, 0x02, v, 2);
}

static void qmi_header(uint8_t *out, uint8_t type, uint16_t t, uint16_t id, unsigned int len)
{
	out[0] = type;
	out[1] = t & 0xff;
	out[2] = t >> 8;
	out[3] = id & 0xff;
	out[4] = id >> 8;
	out[5] = len & 0xff;
	out[6] = len >> 8;
}

static int ctrl(int fd, uint32_t cmd, uint32_t service, uint32_t instance)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	uint32_t pkt[5] = { cmd, service, instance, 0, 0 };

	if (getsockname(fd, (struct sockaddr *)&sq, &sl) < 0)
		return -1;
	sq.sq_port = QRTR_PORT_CTRL;
	return sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&sq, sizeof(sq));
}

/* ---- Registry service ---- */

static struct group *find_group(unsigned int id)
{
	int i;

	for (i = 0; i < ngroups; i++)
		if (groups[i].id == id)
			return &groups[i];
	return NULL;
}

static void registry_request(int fd, const struct sockaddr_qrtr *from, const uint8_t *msg, ssize_t n)
{
	unsigned int id = msg[3] | msg[4] << 8, len, dlen;
	const uint8_t *body = msg + 7, *v, *d;
	uint8_t out[1024], *p = out + 7;
	struct group *g;

	if (msg[0] != QMI_REQUEST)
		return;

	switch (id) {
	case 0x01: {	/* version, as the vendor daemon answers */
		uint32_t minor = 46;
		uint16_t max_id = 6;

		p = put_result(p, 0);
		p = put_tlv(p, 0x03, &minor, 4);
		p = put_tlv(p, 0x04, &max_id, 2);
		break;
	}
	case 0x04:	/* group read */
		v = tlv(body, n - 7, 0x01, &len);
		g = v && len == 2 ? find_group(v[0] | v[1] << 8) : NULL;
		if (!g || g->offset + g->size > reglen) {
			p = put_result(p, 1);
		} else {
			uint8_t data[2 + 256];
			uint16_t gid = g->id;

			data[0] = g->size & 0xff;
			data[1] = g->size >> 8;
			memcpy(data + 2, reg + g->offset, g->size);
			p = put_result(p, 0);
			p = put_tlv(p, 0x03, &gid, 2);
			p = put_tlv(p, 0x04, data, 2 + g->size);
		}
		break;
	case 0x05:	/* group write: kept in memory only */
		v = tlv(body, n - 7, 0x01, &len);
		d = tlv(body, n - 7, 0x02, &dlen);
		g = v && len == 2 ? find_group(v[0] | v[1] << 8) : NULL;
		if (g && d && dlen >= 2) {
			unsigned int cnt = d[0] | d[1] << 8;

			if (cnt <= g->size && cnt <= dlen - 2)
				memcpy(reg + g->offset, d + 2, cnt);
		}
		p = put_result(p, 0);
		break;
	default:
		log_msg("registry: unhandled request 0x%02x", id);
		p = put_result(p, 1);
		break;
	}
	qmi_header(out, QMI_RESPONSE, msg[1] | msg[2] << 8, id, p - (out + 7));
	sendto(fd, out, p - out, 0, (const struct sockaddr *)from, sizeof(*from));
}

/* ---- Outputs ---- */

static void write_file(const char *name, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void write_file(const char *name, const char *fmt, ...)
{
	char path[256], tmp[260];
	va_list ap;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", outdir, name);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	f = fopen(tmp, "w");
	if (!f)
		return;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fclose(f);
	rename(tmp, path);
}

static void emit(int fd, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev = { .type = type, .code = code, .value = value };

	if (fd >= 0 && write(fd, &ev, sizeof(ev)) < 0)
		log_msg("uinput write: %s", strerror(errno));
}

static int uinput_open(const char *name, int accel)
{
	struct uinput_setup us = { .id = { .bustype = BUS_HOST, .vendor = 0x5332 } };
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	int code;

	if (fd < 0) {
		log_msg("/dev/uinput: %s", strerror(errno));
		return -1;
	}
	snprintf(us.name, sizeof(us.name), "%s", name);
	if (accel) {
		ioctl(fd, UI_SET_EVBIT, EV_ABS);
		ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER);
		for (code = ABS_X; code <= ABS_Z; code++) {
			struct uinput_abs_setup abs = {
				.code = code,
				.absinfo = { .minimum = -16000, .maximum = 16000,
					     .resolution = 1000 },	/* units per g */
			};

			ioctl(fd, UI_SET_ABSBIT, code);
			ioctl(fd, UI_ABS_SETUP, &abs);
		}
		us.id.product = 1;
	} else {
		ioctl(fd, UI_SET_EVBIT, EV_SW);
		ioctl(fd, UI_SET_SWBIT, SW_FRONT_PROXIMITY);
		us.id.product = 2;
	}
	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		log_msg("uinput %s: %s", name, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* ---- Sensor manager client ---- */

static void request_report(int fd, const struct sockaddr_qrtr *smgr, const struct stream *s)
{
	uint8_t out[128], *p = out + 7;
	uint8_t action = 1;			/* add */
	uint16_t rate = s->rate;
	uint8_t buffer = 0;
	uint8_t items[1 + 19] = { 1 };		/* one item */

	items[1] = s->sensor;
	items[2] = s->data_type;
	items[4] = 1;				/* decimation: latest sample */

	p = put_tlv(p, 0x01, &s->report_id, 1);
	p = put_tlv(p, 0x02, &action, 1);
	p = put_tlv(p, 0x03, &rate, 2);
	p = put_tlv(p, 0x04, &buffer, 1);
	p = put_tlv(p, 0x05, items, sizeof(items));
	qmi_header(out, QMI_REQUEST, ++txn, SMGR_REPORT, p - (out + 7));
	sendto(fd, out, p - out, 0, (const struct sockaddr *)smgr, sizeof(*smgr));
}

static void report_ind(const uint8_t *body, size_t n)
{
	unsigned int len, i;
	const uint8_t *v = tlv(body, n, 0x04, &len);

	/* Item array: count, then 21-byte items */
	if (!v || len < 1)
		return;
	for (i = 0; i < v[0] && 1 + 21 * (i + 1) <= len; i++) {
		const uint8_t *it = v + 1 + 21 * i;
		int32_t raw[3];
		double x, y, z;

		memcpy(raw, it + 2, sizeof(raw));
		x = raw[0] / Q16;
		y = raw[1] / Q16;
		z = raw[2] / Q16;

		if (it[0] == SENSOR_ACCEL && it[1] == 0) {
			/* Sensor axes report gravity's direction; map to Linux axes */
			double ax = y, ay = x, az = -z;

			write_file("accel", "%.3f %.3f %.3f\n", ax, ay, az);
			emit(ui_accel, EV_ABS, ABS_X, ax * 1000 / STANDARD_GRAVITY);
			emit(ui_accel, EV_ABS, ABS_Y, ay * 1000 / STANDARD_GRAVITY);
			emit(ui_accel, EV_ABS, ABS_Z, az * 1000 / STANDARD_GRAVITY);
			emit(ui_accel, EV_SYN, SYN_REPORT, 0);
		} else if (it[0] == SENSOR_PROX_LIGHT && it[1] == 0) {
			int near = raw[0] != 0;

			write_file("proximity", "%d %.0f\n", near, y);
			if (near != last_prox) {
				emit(ui_prox, EV_SW, SW_FRONT_PROXIMITY, near);
				emit(ui_prox, EV_SYN, SYN_REPORT, 0);
				last_prox = near;
			}
		} else if (it[0] == SENSOR_PROX_LIGHT && it[1] == 1) {
			write_file("light", "%.1f\n", x);
		} else if (it[0] == SENSOR_PRESSURE && it[1] == 0) {
			write_file("pressure", "%.2f\n", x);
		}
	}
}

static int load_registry(const char *gpath, const char *rpath)
{
	FILE *f = fopen(gpath, "r");
	char line[128];

	if (!f) {
		log_msg("%s: %s", gpath, strerror(errno));
		return -1;
	}
	while (ngroups < MAX_GROUPS && fgets(line, sizeof(line), f))
		if (line[0] != '#' &&
		    sscanf(line, "%i %i %i", &groups[ngroups].id, &groups[ngroups].size,
			   &groups[ngroups].offset) == 3)
			ngroups++;
	fclose(f);

	f = fopen(rpath, "rb");
	if (!f) {
		log_msg("%s: %s", rpath, strerror(errno));
		return -1;
	}
	fseek(f, 0, SEEK_END);
	reglen = ftell(f);
	rewind(f);
	reg = malloc(reglen);
	if (!reg || fread(reg, 1, reglen, f) != reglen) {
		log_msg("cannot read %s", rpath);
		return -1;
	}
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	const char *gpath = "/etc/s22-sensord/groups.txt";
	const char *rpath = "/persist/sensors/sns.reg";
	struct sockaddr_qrtr smgr = { 0 };
	int srv, cli, opt;
	unsigned int i;

	while ((opt = getopt(argc, argv, "g:r:o:a:p:l:b:")) != -1) {
		switch (opt) {
		case 'g': gpath = optarg; break;
		case 'r': rpath = optarg; break;
		case 'o': outdir = optarg; break;
		case 'a': streams[0].rate = atoi(optarg); break;
		case 'p': streams[1].rate = atoi(optarg); break;
		case 'l': streams[2].rate = atoi(optarg); break;
		case 'b': streams[3].rate = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: %s [-g GROUPS] [-r SNS.REG] [-o OUTDIR] "
				"[-a HZ] [-p HZ] [-l HZ] [-b HZ]\n", argv[0]);
			return 2;
		}
	}
	if (load_registry(gpath, rpath))
		return 1;
	mkdir(outdir, 0755);
	if (streams[0].rate)
		ui_accel = uinput_open("S22 Flip accelerometer", 1);
	if (streams[1].rate)
		ui_prox = uinput_open("S22 Flip proximity", 0);

	/* Services for the ADSP, announced time service first as the vendor daemon does */
	srv = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	cli = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (srv < 0 || cli < 0) {
		log_msg("socket: %s", strerror(errno));
		return 1;
	}
	ctrl(srv, QRTR_TYPE_NEW_SERVER, TIME_SERVICE, TIME_VERSION | TIME_INSTANCE << 8);
	ctrl(srv, QRTR_TYPE_NEW_SERVER, REG_SERVICE, REG_VERSION);
	ctrl(cli, QRTR_TYPE_NEW_LOOKUP, SMGR_SERVICE, 0);
	log_msg("registry: %d groups, %zu bytes; waiting for the sensor manager", ngroups, reglen);

	for (;;) {
		struct pollfd pfd[2] = { { .fd = srv, .events = POLLIN }, { .fd = cli, .events = POLLIN } };
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		uint8_t buf[65536];
		ssize_t n;

		if (poll(pfd, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pfd[0].revents & POLLIN) {
			n = recvfrom(srv, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
			if (n >= 7 && from.sq_port != QRTR_PORT_CTRL)
				registry_request(srv, &from, buf, n);
		}
		if (!(pfd[1].revents & POLLIN))
			continue;
		fl = sizeof(from);
		n = recvfrom(cli, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n < 0)
			continue;
		if (from.sq_port == QRTR_PORT_CTRL && n >= 20) {
			uint32_t pkt[5];

			memcpy(pkt, buf, sizeof(pkt));
			/* Only the ADSP's SMGR; WCNSS publishes an unrelated stub */
			if (pkt[1] != SMGR_SERVICE || pkt[2] >> 8 != SMGR_INSTANCE)
				continue;
			if (pkt[0] == QRTR_TYPE_NEW_SERVER) {
				smgr.sq_family = AF_QIPCRTR;
				smgr.sq_node = pkt[3];
				smgr.sq_port = pkt[4];
				log_msg("sensor manager at %u:%u", smgr.sq_node, smgr.sq_port);
				for (i = 0; i < NSTREAMS; i++)
					if (streams[i].rate)
						request_report(cli, &smgr, &streams[i]);
			} else if (pkt[0] == QRTR_TYPE_DEL_SERVER) {
				log_msg("sensor manager gone");
				smgr.sq_family = 0;
			}
			continue;
		}
		if (n < 7 || !smgr.sq_family || from.sq_node != smgr.sq_node)
			continue;
		if (buf[0] == QMI_INDICATION && (buf[3] | buf[4] << 8) == SMGR_REPORT_IND)
			report_ind(buf + 7, n - 7);
		else if (buf[0] == QMI_RESPONSE && (buf[3] | buf[4] << 8) == SMGR_REPORT) {
			unsigned int len;
			const uint8_t *r = tlv(buf + 7, n - 7, 0x02, &len);

			if (!r || r[0] || r[1])
				log_msg("report request rejected");
		}
	}
	return 1;
}
