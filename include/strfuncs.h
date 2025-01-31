/*
 * strfuncs.h - string functions
 *
 * This file is Copyright by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */
#ifndef _GPSD_STRFUNCS_H_
#define _GPSD_STRFUNCS_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "compiler.h"


static inline bool str_starts_with(const char *str, const char *prefix)
{
    return 0 == strncmp(str, prefix, strlen(prefix));
}


PRINTF_FUNC(3, 4)
static inline void str_appendf(char *str, size_t alloc_size,
                               const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(str + strlen(str), alloc_size - strlen(str), format, ap);
    va_end(ap);
}


static inline void str_vappendf(char *str, size_t alloc_size,
                                const char *format, va_list ap)
{
    (void) vsnprintf(str + strlen(str), alloc_size - strlen(str), format, ap);
}


static inline void str_rstrip_char(char *str, char ch)
{
    if (0 != strlen(str) &&
        str[strlen(str) - 1] == ch) {
        str[strlen(str) - 1] = '\0';
    }
}

/* memset() for a volatile destination
 * dest = destination
 * c = fill character
 * count = sizeof(dest)
 */
static inline void memset_volatile(volatile void *dest, char c, size_t count)
{
    volatile char *ptr = (volatile char*)dest;
    while (0 < count--) {
        *ptr++ = c;
    }
}
#endif  // _GPSD_STRFUNCS_H_
