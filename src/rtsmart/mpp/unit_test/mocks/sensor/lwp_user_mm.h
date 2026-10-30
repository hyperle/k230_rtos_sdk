/* SPDX-License-Identifier: Apache-2.0 */
/* Driver tests do not cross the RT-Smart userspace boundary. Avoid importing
 * RT-Smart's POSIX declarations into the host libc; no LWP function is used. */
