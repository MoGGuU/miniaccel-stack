// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <miniaccel_uapi.h>

#define MINIACCEL_DEFAULT_DEV "/dev/miniaccel0"
#define DEFAULT_THREADS 8U
#define DEFAULT_LOOPS 50U
#define MAX_THREADS 128U

struct worker_arg {
	const char *devnode;
	unsigned int thread_id;
	unsigned int loops;
	unsigned int failures;
};

static const uint32_t factorials[] = {
	1,
	1,
	2,
	6,
	24,
	120,
	720,
	5040,
	40320,
	362880,
	3628800,
	39916800,
	479001600,
};

static int parse_uint(const char *text, const char *name, unsigned int *value)
{
	unsigned long parsed;
	char *end;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || *end || parsed > UINT32_MAX) {
		fprintf(stderr, "invalid %s: %s\n", name, text);
		return -1;
	}

	*value = (unsigned int)parsed;
	return 0;
}

static void *worker_main(void *opaque)
{
	struct worker_arg *arg = opaque;
	int fd;
	unsigned int i;

	fd = open(arg->devnode, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "thread %u: open %s failed: %s\n",
			arg->thread_id, arg->devnode, strerror(errno));
		arg->failures++;
		return NULL;
	}

	for (i = 0; i < arg->loops; i++) {
		uint32_t input = (arg->thread_id + i) %
				 (sizeof(factorials) / sizeof(factorials[0]));
		struct miniaccel_run_sync run = {
			.input = input,
			.timeout_ms = 1000,
		};

		if (ioctl(fd, MINIACCEL_IOCTL_RUN_SYNC, &run) < 0) {
			fprintf(stderr,
				"thread %u loop %u: RUN_SYNC(%u) failed: %s\n",
				arg->thread_id, i, input, strerror(errno));
			arg->failures++;
			continue;
		}

		if (run.output != factorials[input]) {
			fprintf(stderr,
				"thread %u loop %u: RUN_SYNC(%u) output=%u expected=%u\n",
				arg->thread_id, i, input, run.output,
				factorials[input]);
			arg->failures++;
		}
	}

	close(fd);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *devnode = MINIACCEL_DEFAULT_DEV;
	unsigned int threads = DEFAULT_THREADS;
	unsigned int loops = DEFAULT_LOOPS;
	struct worker_arg *args;
	pthread_t *tids;
	unsigned int failures = 0;
	unsigned int i;

	if (argc > 4) {
		fprintf(stderr, "Usage: %s [devnode] [threads] [loops]\n",
			argv[0]);
		return 2;
	}

	if (argc >= 2)
		devnode = argv[1];
	if (argc >= 3 && parse_uint(argv[2], "threads", &threads))
		return 2;
	if (argc >= 4 && parse_uint(argv[3], "loops", &loops))
		return 2;

	if (!threads || threads > MAX_THREADS || !loops) {
		fprintf(stderr, "invalid threads/loops: threads=%u loops=%u\n",
			threads, loops);
		return 2;
	}

	args = calloc(threads, sizeof(*args));
	tids = calloc(threads, sizeof(*tids));
	if (!args || !tids) {
		perror("calloc");
		free(args);
		free(tids);
		return 1;
	}

	for (i = 0; i < threads; i++) {
		args[i].devnode = devnode;
		args[i].thread_id = i;
		args[i].loops = loops;
		if (pthread_create(&tids[i], NULL, worker_main, &args[i])) {
			fprintf(stderr, "pthread_create(%u) failed\n", i);
			args[i].failures++;
		}
	}

	for (i = 0; i < threads; i++) {
		if (tids[i])
			pthread_join(tids[i], NULL);
		failures += args[i].failures;
	}

	free(args);
	free(tids);

	if (failures) {
		fprintf(stderr, "concurrent-run-sync failures=%u\n", failures);
		return 1;
	}

	printf("concurrent-run-sync-ok threads=%u loops=%u total=%u\n",
	       threads, loops, threads * loops);
	return 0;
}
