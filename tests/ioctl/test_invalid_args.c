// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <miniaccel_uapi.h>

#define MINIACCEL_DEFAULT_DEV "/dev/miniaccel0"

static int expect_ioctl_errno(int fd, unsigned long request, void *arg,
			      int expected_errno, const char *name)
{
	int ret;

	errno = 0;
	ret = ioctl(fd, request, arg);
	if (ret != -1 || errno != expected_errno) {
		fprintf(stderr,
			"%s: expected errno=%d (%s), got ret=%d errno=%d (%s)\n",
			name, expected_errno, strerror(expected_errno), ret,
			errno, strerror(errno));
		return -1;
	}

	printf("%s-ok\n", name);
	return 0;
}

int main(int argc, char **argv)
{
	const char *devnode = MINIACCEL_DEFAULT_DEV;
	struct miniaccel_query query = {};
	struct miniaccel_run_sync run = {};
	int fd;
	int ret = 0;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [devnode]\n", argv[0]);
		return 2;
	}

	if (argc == 2)
		devnode = argv[1];

	fd = open(devnode, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", devnode,
			strerror(errno));
		return 1;
	}

	ret |= expect_ioctl_errno(fd, 0x12345678, &query, ENOTTY,
				  "invalid-ioctl-enotty");
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_QUERY, NULL, EFAULT,
				  "query-null-efault");
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_RUN_SYNC, NULL, EFAULT,
				  "run-sync-null-efault");

	run = (struct miniaccel_run_sync){
		.input = 5,
		.timeout_ms = 1000,
		.reserved = 1,
	};
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_RUN_SYNC, &run, EINVAL,
				  "run-sync-reserved-einval");

	run = (struct miniaccel_run_sync){
		.input = 5,
		.timeout_ms = 0,
	};
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_RUN_SYNC, &run, EINVAL,
				  "run-sync-timeout-zero-einval");

	run = (struct miniaccel_run_sync){
		.input = 5,
		.timeout_ms = 10001,
	};
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_RUN_SYNC, &run, EINVAL,
				  "run-sync-timeout-too-large-einval");

	run = (struct miniaccel_run_sync){
		.input = 13,
		.timeout_ms = 1000,
	};
	ret |= expect_ioctl_errno(fd, MINIACCEL_IOCTL_RUN_SYNC, &run, EINVAL,
				  "run-sync-input-too-large-einval");

	close(fd);

	if (ret)
		return 1;

	puts("invalid-args-ok");
	return 0;
}
