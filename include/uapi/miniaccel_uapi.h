/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef MINIACCEL_UAPI_H
#define MINIACCEL_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MINIACCEL_CAP_RUN_SYNC (1ULL << 0)

struct miniaccel_query {
	__u32 device_id;
	__u32 version;
	__u64 capabilities;
	__u64 reserved[2];
};

struct miniaccel_run_sync {
	__u32 input;
	__u32 output;
	__u32 timeout_ms;
	__u32 reserved;
};

#define MINIACCEL_IOCTL_BASE 'M'
#define MINIACCEL_IOCTL_QUERY \
	_IOR(MINIACCEL_IOCTL_BASE, 0x00, struct miniaccel_query)
#define MINIACCEL_IOCTL_RUN_SYNC \
	_IOWR(MINIACCEL_IOCTL_BASE, 0x01, struct miniaccel_run_sync)

#endif
