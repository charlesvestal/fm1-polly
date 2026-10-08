/* SPDX-License-Identifier: GPL-3.0-only */
/* <string.h> for Sonivox on the FM-1 (firmware build only: the toolchain has no C library headers):
 * Felucca's libc.c has these three; strcmp is here. */
#pragma once
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
