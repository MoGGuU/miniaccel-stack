// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <miniaccel_uapi.h>

#define MINIACCEL_DEFAULT_DEV "/dev/miniaccel0"
#define MINIACCEL_DEFAULT_TIMEOUT_MS 1000U

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <input> [timeout_ms] [devnode]\n", prog);
	fprintf(stderr, "Default timeout_ms: %u\n",
		MINIACCEL_DEFAULT_TIMEOUT_MS);
	fprintf(stderr, "Default devnode: %s\n", MINIACCEL_DEFAULT_DEV);
}

static int parse_u32(const char *text, const char *name, uint32_t *value)
{
	unsigned long parsed;
	char *end;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || *end || parsed > UINT32_MAX) {
		fprintf(stderr, "invalid %s: %s\n", name, text);
		return -1;
	}

	*value = (uint32_t)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	const char *devnode = MINIACCEL_DEFAULT_DEV;
	struct miniaccel_run_sync run = {
		.timeout_ms = MINIACCEL_DEFAULT_TIMEOUT_MS,
	};
	int fd;
	int ret;

	if (argc < 2 || argc > 4) {
		usage(argv[0]);
		return 2;
	}

	if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
		return 0;
	}

	if (parse_u32(argv[1], "input", &run.input))
		return 2;

	if (argc >= 3 && parse_u32(argv[2], "timeout_ms", &run.timeout_ms))
		return 2;

	if (argc == 4)
		devnode = argv[3];

	fd = open(devnode, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", devnode,
			strerror(errno));
		return 1;
	}

	ret = ioctl(fd, MINIACCEL_IOCTL_RUN_SYNC, &run);
	if (ret < 0) {
		fprintf(stderr, "ioctl RUN_SYNC failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	printf("input=%u output=%u timeout_ms=%u\n", run.input, run.output,
	       run.timeout_ms);

	close(fd);
	return 0;
}
