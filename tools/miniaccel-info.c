// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <miniaccel_uapi.h>

#define MINIACCEL_DEFAULT_DEV "/dev/miniaccel0"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [devnode]\n", prog);
	fprintf(stderr, "Default devnode: %s\n", MINIACCEL_DEFAULT_DEV);
}

int main(int argc, char **argv)
{
	const char *devnode = MINIACCEL_DEFAULT_DEV;
	struct miniaccel_query query = {};
	int fd;
	int ret;

	if (argc > 2) {
		usage(argv[0]);
		return 2;
	}

	if (argc == 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(argv[0]);
			return 0;
		}
		devnode = argv[1];
	}

	fd = open(devnode, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", devnode,
			strerror(errno));
		return 1;
	}

	ret = ioctl(fd, MINIACCEL_IOCTL_QUERY, &query);
	if (ret < 0) {
		fprintf(stderr, "ioctl QUERY failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	printf("device_id=0x%04x version=0x%08x capabilities=0x%016llx\n",
	       query.device_id, query.version,
	       (unsigned long long)query.capabilities);

	close(fd);
	return 0;
}
