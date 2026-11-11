/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <string.h>
/* In-process copies replace the kernel/userspace boundary in host tests. */
static inline size_t lwp_get_from_user(void *to, const void *from, size_t size)
{ memcpy(to, from, size); return size; }
static inline size_t lwp_put_to_user(void *to, const void *from, size_t size)
{ memcpy(to, from, size); return size; }
