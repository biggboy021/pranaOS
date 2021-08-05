/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef _KERNEL_LIBKERNEL_BITS_SYS_MMAN_H
#define _KERNEL_LIBKERNEL_BITS_SYS_MMAN_H

// includes
#include <libkernel/types.h>

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_STACK 0x40

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define PROT_NONE 0x0

struct mmap_params {
    void* addr;
    size_t size;
    int prot;
    int flags;
    int fd;
    off_t offset;
};
typedef struct mmap_params mmap_params_t;

#endif 