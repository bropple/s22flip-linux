// SPDX-License-Identifier: GPL-2.0-only
/*
 * sns-reg-serve: a minimal sensor registry service (QMI service 0x10f,
 * version 2) for Qualcomm ADSP sensor stacks of the SMGR generation.
 *
 * The ADSP's sensor manager reads its configuration (which sensors sit on
 * which bus, calibration...) from this service, which Android's sensor
 * daemon provides. Without it the sensor manager never finishes starting.
 *
 *   sns-reg-serve GROUPS REGFILE [SECONDS]
 *
 * GROUPS is a text file of "group-id size offset" lines (decimal or 0x hex)
 * describing where each group lives in REGFILE, the flat registry image
 * (sns.reg from the phone's persist partition). Writes are accepted and
 * applied in memory only.
 *
 * Build: aarch64-linux-gnu-gcc -static -O2 -o sns-reg-serve sns-reg-serve.c
 */
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif
#define QRTR_PORT_CTRL		0xfffffffeu
#define QRTR_TYPE_NEW_SERVER	4

#define SNS_REG_SERVICE		0x10f
#define SNS_REG_VERSION		2

#define MSG_VERSION		0x01
#define MSG_GROUP_READ		0x04
#define MSG_GROUP_WRITE		0x05

#define MAX_GROUPS		512

struct sockaddr_qrtr {
	unsigned short sq_family;
	uint32_t sq_node;
	uint32_t sq_port;
};

struct group {
	unsigned int id, size, offset;
};

static struct group groups[MAX_GROUPS];
static int ngroups;
static uint8_t *reg;
static size_t reglen;

static struct group *find_group(unsigned int id)
{
	int i;

	for (i = 0; i < ngroups; i++)
		if (groups[i].id == id)
			return &groups[i];
	return NULL;
}

/* Find TLV @type in a QMI message body; returns its value and length */
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

/* This interface's result TLV carries only the 16-bit result, no error code */
static uint8_t *put_result(uint8_t *p, uint16_t result)
{
	uint8_t v[2] = { result & 0xff, result >> 8 };

	return put_tlv(p, 0x02, v, 2);
}

static void reply(int fd, const struct sockaddr_qrtr *to, const uint8_t *req,
		  uint8_t *out, uint8_t *end)
{
	unsigned int len = end - (out + 7);

	out[0] = 2;		/* response */
	out[1] = req[1];	/* transaction */
	out[2] = req[2];
	out[3] = req[3];	/* message ID */
	out[4] = req[4];
	out[5] = len & 0xff;
	out[6] = len >> 8;
	sendto(fd, out, end - out, 0, (const struct sockaddr *)to, sizeof(*to));
}

static void handle(int fd, const struct sockaddr_qrtr *from, const uint8_t *msg, ssize_t n)
{
	unsigned int id = msg[3] | msg[4] << 8, len;
	const uint8_t *body = msg + 7, *v;
	uint8_t out[1024], *p = out + 7;
	struct group *g;

	if (msg[0] != 0)
		return;

	switch (id) {
	case MSG_VERSION: {
		/* As the vendor daemon answers: interface minor version, max message ID */
		uint32_t minor = 46;
		uint16_t max_id = 6;

		p = put_result(p, 0);
		p = put_tlv(p, 0x03, &minor, 4);
		p = put_tlv(p, 0x04, &max_id, 2);
		printf("version request\n");
		break;
	}
	case MSG_GROUP_READ:
		v = tlv(body, n - 7, 0x01, &len);
		g = v && len == 2 ? find_group(v[0] | v[1] << 8) : NULL;
		if (!g || g->offset + g->size > reglen) {
			printf("group read %u: unknown\n", v && len == 2 ? v[0] | v[1] << 8 : 0);
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
			printf("group read %u: %u bytes\n", g->id, g->size);
		}
		break;
	case MSG_GROUP_WRITE:
		v = tlv(body, n - 7, 0x01, &len);
		g = v && len == 2 ? find_group(v[0] | v[1] << 8) : NULL;
		{
			unsigned int dlen;
			const uint8_t *d = tlv(body, n - 7, 0x02, &dlen);

			if (g && d && dlen >= 2) {
				unsigned int cnt = d[0] | d[1] << 8;

				if (cnt <= g->size && cnt <= dlen - 2)
					memcpy(reg + g->offset, d + 2, cnt);
				printf("group write %u: %u bytes (kept in memory)\n", g->id, cnt);
			} else {
				printf("group write: unknown group\n");
			}
		}
		p = put_result(p, 0);
		break;
	default:
		printf("unhandled request 0x%02x:", id);
		for (len = 7; len < n; len++)
			printf(" %02x", msg[len]);
		printf("\n");
		p = put_result(p, 1);
		break;
	}
	fflush(stdout);
	reply(fd, from, msg, out, p);
}

int main(int argc, char **argv)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	uint32_t pkt[5] = { 0 };
	uint8_t buf[65536];
	int fd, secs = 0;
	time_t end;
	FILE *f;

	if (argc < 3) {
		fprintf(stderr, "usage: %s GROUPS REGFILE [SECONDS]\n", argv[0]);
		return 2;
	}
	if (argc > 3)
		secs = atoi(argv[3]);

	f = fopen(argv[1], "r");
	if (!f) {
		perror(argv[1]);
		return 1;
	}
	while (ngroups < MAX_GROUPS &&
	       fscanf(f, "%i %i %i", &groups[ngroups].id, &groups[ngroups].size,
		      &groups[ngroups].offset) == 3)
		ngroups++;
	fclose(f);

	f = fopen(argv[2], "rb");
	if (!f) {
		perror(argv[2]);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	reglen = ftell(f);
	rewind(f);
	reg = malloc(reglen);
	if (!reg || fread(reg, 1, reglen, f) != reglen) {
		fprintf(stderr, "cannot read %s\n", argv[2]);
		return 1;
	}
	fclose(f);

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0 || getsockname(fd, (struct sockaddr *)&sq, &sl) < 0) {
		perror("socket");
		return 1;
	}
	pkt[0] = QRTR_TYPE_NEW_SERVER;
	pkt[1] = SNS_REG_SERVICE;
	pkt[2] = SNS_REG_VERSION;	/* instance 0 */
	sq.sq_port = QRTR_PORT_CTRL;
	if (sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&sq, sizeof(sq)) < 0) {
		perror("announce");
		return 1;
	}
	printf("sensor registry: %d groups, %zu bytes\n", ngroups, reglen);
	fflush(stdout);

	end = secs ? time(NULL) + secs : 0;
	while (!end || time(NULL) < end) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		ssize_t n;

		if (poll(&pfd, 1, 200) <= 0)
			continue;
		n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n >= 7)
			handle(fd, &from, buf, n);
	}
	return 0;
}
