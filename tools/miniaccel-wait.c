// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MINIACCEL_DEFAULT_DEV "/dev/miniaccel0"
#define MINIACCEL_DEFAULT_TIMEOUT_MS 3000

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--timeout-ms N] [--dev DEVNODE]\n", prog);
	fprintf(stderr, "Default timeout_ms: %d\n",
		MINIACCEL_DEFAULT_TIMEOUT_MS);
	fprintf(stderr, "Default devnode: %s\n", MINIACCEL_DEFAULT_DEV);
}

static int parse_timeout_ms(const char *text, int *timeout_ms)
{
	long parsed;
	char *end;

	errno = 0;
	parsed = strtol(text, &end, 0);
	if (errno || *end || parsed < -1 || parsed > INT_MAX) {
		fprintf(stderr, "invalid timeout_ms: %s\n", text);
		return -1;
	}

	*timeout_ms = (int)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	const char *devnode = MINIACCEL_DEFAULT_DEV;
	int timeout_ms = MINIACCEL_DEFAULT_TIMEOUT_MS;
	struct pollfd pfd = {
		.fd = -1,
		.events = POLLIN | POLLRDNORM,
	};
	int ret;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		}

		if (!strcmp(argv[i], "--timeout-ms")) {
			if (++i >= argc) {
				usage(argv[0]);
				return 2;
			}
			if (parse_timeout_ms(argv[i], &timeout_ms))
				return 2;
			continue;
		}

		if (!strncmp(argv[i], "--timeout-ms=", 13)) {
			if (parse_timeout_ms(argv[i] + 13, &timeout_ms))
				return 2;
			continue;
		}

		if (!strcmp(argv[i], "--dev")) {
			if (++i >= argc) {
				usage(argv[0]);
				return 2;
			}
			devnode = argv[i];
			continue;
		}

		if (!strncmp(argv[i], "--dev=", 6)) {
			devnode = argv[i] + 6;
			continue;
		}

		usage(argv[0]);
		return 2;
	}

	pfd.fd = open(devnode, O_RDONLY | O_CLOEXEC);
	if (pfd.fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", devnode,
			strerror(errno));
		return 1;
	}

	ret = poll(&pfd, 1, timeout_ms);
	if (ret < 0) {
		fprintf(stderr, "poll failed: %s\n", strerror(errno));
		close(pfd.fd);
		return 1;
	}

	if (!ret) {
		printf("timeout timeout_ms=%d\n", timeout_ms);
		close(pfd.fd);
		return 0;
	}

	printf("revents=0x%x", pfd.revents);
	if (pfd.revents & (POLLIN | POLLRDNORM))
		printf(" readable");
	if (pfd.revents & POLLERR)
		printf(" error");
	if (pfd.revents & POLLHUP)
		printf(" hangup");
	if (pfd.revents & POLLNVAL)
		printf(" invalid");
	printf("\n");

	close(pfd.fd);
	return (pfd.revents & POLLNVAL) ? 1 : 0;
}
