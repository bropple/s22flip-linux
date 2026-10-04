// SPDX-License-Identifier: GPL-2.0-only
/*
 * qmisend: send one raw QMI request to a QRTR service and print the
 * response and any indications that follow, as hex.
 *
 *   qmisend NODE PORT MSGID [TLV-HEX] [SECONDS]
 *   qmisend serve SERVICE VERSION INSTANCE [SECONDS]
 *
 * The serve form announces a QMI service and prints the requests it gets,
 * without answering them.
 *
 * TLV-HEX is the message body (type, little-endian length, value...),
 * e.g. "0101000a" for TLV 0x01 with one byte 0x0a. Find NODE and PORT with
 * qrtr-lookup.
 *
 * Build: aarch64-linux-gnu-gcc -static -O2 -o qmisend qmisend.c
 */
#include <errno.h>
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

struct sockaddr_qrtr {
	unsigned short sq_family;
	uint32_t sq_node;
	uint32_t sq_port;
};

static const char *kind(int t)
{
	switch (t) {
	case 0: return "request";
	case 2: return "response";
	case 4: return "indication";
	default: return "?";
	}
}

static int hexbytes(const char *s, uint8_t *out, int max)
{
	int n = 0;
	unsigned int b;

	while (*s && n < max) {
		if (*s == ' ' || *s == ':') {
			s++;
			continue;
		}
		if (sscanf(s, "%2x", &b) != 1)
			return -1;
		out[n++] = b;
		s += 2;
	}
	return n;
}

#define QRTR_PORT_CTRL		0xfffffffeu
#define QRTR_TYPE_NEW_SERVER	4

static void dump(const uint8_t *buf, ssize_t n, const struct sockaddr_qrtr *from)
{
	ssize_t i;

	printf("%s from %u:%u txn=%u id=0x%02x len=%u:", kind(buf[0]),
	       from->sq_node, from->sq_port, buf[1] | buf[2] << 8,
	       buf[3] | buf[4] << 8, buf[5] | buf[6] << 8);
	for (i = 7; i < n; i++)
		printf(" %02x", buf[i]);
	printf("\n");
	fflush(stdout);
}

static int serve(int argc, char **argv)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	uint32_t pkt[5] = { 0 };
	uint8_t buf[65536];
	int fd, secs = 30;
	time_t end;

	if (argc < 5) {
		fprintf(stderr, "usage: %s serve SERVICE VERSION INSTANCE [SECONDS]\n", argv[0]);
		return 2;
	}
	if (argc > 5)
		secs = atoi(argv[5]);

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0 || getsockname(fd, (struct sockaddr *)&sq, &sl) < 0) {
		perror("socket");
		return 1;
	}

	/* Announce to the name server; it takes node and port from the sender */
	pkt[0] = QRTR_TYPE_NEW_SERVER;
	pkt[1] = strtoul(argv[2], NULL, 0);
	pkt[2] = strtoul(argv[3], NULL, 0) | strtoul(argv[4], NULL, 0) << 8;
	sq.sq_port = QRTR_PORT_CTRL;
	if (sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&sq, sizeof(sq)) < 0) {
		perror("announce");
		return 1;
	}
	printf("serving service %u version %u instance %u\n", pkt[1],
	       pkt[2] & 0xff, pkt[2] >> 8);
	fflush(stdout);

	end = time(NULL) + secs;
	while (time(NULL) < end) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		ssize_t n;

		if (poll(&pfd, 1, 200) <= 0)
			continue;
		n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n >= 7)
			dump(buf, n, &from);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct sockaddr_qrtr sq = { .sq_family = AF_QIPCRTR };
	uint8_t buf[65536];
	int fd, len = 0, secs = 3;
	uint16_t msgid;
	time_t end;

	if (argc > 1 && !strcmp(argv[1], "serve"))
		return serve(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "whoami")) {
		struct sockaddr_qrtr me;
		socklen_t ml = sizeof(me);

		fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
		if (fd < 0 || getsockname(fd, (struct sockaddr *)&me, &ml) < 0) {
			perror("socket");
			return 1;
		}
		printf("local node %u port %u\n", me.sq_node, me.sq_port);
		return 0;
	}
	if (argc < 4) {
		fprintf(stderr, "usage: %s NODE PORT MSGID [TLV-HEX] [SECONDS]\n", argv[0]);
		return 2;
	}
	sq.sq_node = strtoul(argv[1], NULL, 0);
	sq.sq_port = strtoul(argv[2], NULL, 0);
	msgid = strtoul(argv[3], NULL, 0);
	if (argc > 4 && argv[4][0]) {
		len = hexbytes(argv[4], buf + 7, sizeof(buf) - 7);
		if (len < 0) {
			fprintf(stderr, "bad hex\n");
			return 2;
		}
	}
	if (argc > 5)
		secs = atoi(argv[5]);

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	/* QMI header: type, transaction, message ID, length (little endian) */
	buf[0] = 0;
	buf[1] = 1;
	buf[2] = 0;
	buf[3] = msgid & 0xff;
	buf[4] = msgid >> 8;
	buf[5] = len & 0xff;
	buf[6] = len >> 8;
	if (sendto(fd, buf, 7 + len, 0, (struct sockaddr *)&sq, sizeof(sq)) < 0) {
		perror("sendto");
		return 1;
	}

	end = time(NULL) + secs;
	while (time(NULL) < end) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		ssize_t n;
		int i;

		if (poll(&pfd, 1, 200) <= 0)
			continue;
		n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n < 7)
			continue;
		printf("%s from %u:%u id=0x%02x len=%u (%zd bytes, header %02x %02x %02x):",
		       kind(buf[0]), from.sq_node, from.sq_port,
		       buf[3] | buf[4] << 8, buf[5] | buf[6] << 8, n,
		       buf[0], buf[1], buf[2]);
		for (i = 7; i < n; i++)
			printf(" %02x", buf[i]);
		printf("\n");
		fflush(stdout);
	}
	return 0;
}
