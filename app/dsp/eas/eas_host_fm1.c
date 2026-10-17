/* SPDX-License-Identifier: Apache-2.0 */
/* Sonivox EAS host wrapper for the FM-1 (after host_src/eas_hostmm.c, Copyright (C) 2008 The Android Open
 * Source Project, Apache-2.0): memory from one fixed pool, handed out once at start and never freed (the
 * library allocates only while it starts up); no files (live MIDI only); no reports. */
#include "eas.h"
#include "eas_host.h"
#include "eas_report.h"

#ifdef PLAT_HOST
#define EAS_POOL_ATTR
#else
#define EAS_POOL_ATTR __attribute__((section(".pool")))
#endif
#ifndef EAS_POOL_BYTES
#define EAS_POOL_BYTES (96 * 1024)
#endif
static unsigned long long eas_pool[EAS_POOL_BYTES / 8] EAS_POOL_ATTR;
static EAS_I32 eas_pool_used;
EAS_I32 eas_pool_peak(void) { return eas_pool_used; }

EAS_RESULT EAS_HWInit(EAS_HW_DATA_HANDLE *hwInstData)
{
    *hwInstData = NULL;
    eas_pool_used = 0;
    return EAS_SUCCESS;
}
EAS_RESULT EAS_HWShutdown(EAS_HW_DATA_HANDLE hwInstData)
{
    (void)hwInstData;
    return EAS_SUCCESS;
}

void *EAS_HWMemSet(void *s, int c, EAS_I32 n)
{
    unsigned char *p = s;
    while (n-- > 0)
        *p++ = (unsigned char)c;
    return s;
}
void *EAS_HWMemCpy(void *s1, const void *s2, EAS_I32 n)
{
    unsigned char *d = s1;
    const unsigned char *s = s2;
    while (n-- > 0)
        *d++ = *s++;
    return s1;
}
EAS_I32 EAS_HWMemCmp(const void *s1, const void *s2, EAS_I32 n)
{
    const unsigned char *a = s1, *b = s2;
    for (; n > 0; n--, a++, b++)
        if (*a != *b)
            return (EAS_I32)*a - (EAS_I32)*b;
    return 0;
}

void *EAS_HWMalloc(EAS_HW_DATA_HANDLE hwInstData, EAS_I32 size)
{
    void *p;
    (void)hwInstData;
    size = (size + 7) & ~7;
    if (size <= 0 || eas_pool_used + size > (EAS_I32)sizeof eas_pool)
        return NULL;
    p = (unsigned char *)eas_pool + eas_pool_used;
    eas_pool_used += size;
    return p;
}
void EAS_HWFree(EAS_HW_DATA_HANDLE hwInstData, void *p)
{
    (void)hwInstData;
    (void)p;
}

/* no files */
EAS_RESULT EAS_HWOpenFile(EAS_HW_DATA_HANDLE h, EAS_FILE_LOCATOR l, EAS_FILE_HANDLE *f, EAS_FILE_MODE m)
{
    (void)h; (void)l; (void)m;
    *f = NULL;
    return EAS_ERROR_FEATURE_NOT_AVAILABLE;
}
EAS_RESULT EAS_HWReadFile(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, void *b, EAS_I32 n, EAS_I32 *r)
{
    (void)h; (void)f; (void)b; (void)n;
    *r = 0;
    return EAS_ERROR_FEATURE_NOT_AVAILABLE;
}
EAS_RESULT EAS_HWGetByte(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, void *p) { (void)h; (void)f; (void)p; return EAS_EOF; }
EAS_RESULT EAS_HWGetWord(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, void *p, EAS_BOOL m) { (void)h; (void)f; (void)p; (void)m; return EAS_EOF; }
EAS_RESULT EAS_HWGetDWord(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, void *p, EAS_BOOL m) { (void)h; (void)f; (void)p; (void)m; return EAS_EOF; }
EAS_RESULT EAS_HWFilePos(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, EAS_I32 *p) { (void)h; (void)f; *p = 0; return EAS_ERROR_FEATURE_NOT_AVAILABLE; }
EAS_RESULT EAS_HWFileSeek(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, EAS_I32 p) { (void)h; (void)f; (void)p; return EAS_ERROR_FEATURE_NOT_AVAILABLE; }
EAS_RESULT EAS_HWFileSeekOfs(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, EAS_I32 p) { (void)h; (void)f; (void)p; return EAS_ERROR_FEATURE_NOT_AVAILABLE; }
EAS_RESULT EAS_HWFileLength(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, EAS_I32 *l) { (void)h; (void)f; *l = 0; return EAS_ERROR_FEATURE_NOT_AVAILABLE; }
EAS_RESULT EAS_HWDupHandle(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f, EAS_FILE_HANDLE *p) { (void)h; (void)f; *p = NULL; return EAS_ERROR_FEATURE_NOT_AVAILABLE; }
EAS_RESULT EAS_HWCloseFile(EAS_HW_DATA_HANDLE h, EAS_FILE_HANDLE f) { (void)h; (void)f; return EAS_SUCCESS; }

EAS_RESULT EAS_HWVibrate(EAS_HW_DATA_HANDLE h, EAS_BOOL s) { (void)h; (void)s; return EAS_SUCCESS; }
EAS_RESULT EAS_HWLED(EAS_HW_DATA_HANDLE h, EAS_BOOL s) { (void)h; (void)s; return EAS_SUCCESS; }
EAS_RESULT EAS_HWBackLight(EAS_HW_DATA_HANDLE h, EAS_BOOL s) { (void)h; (void)s; return EAS_SUCCESS; }
EAS_BOOL EAS_HWYield(EAS_HW_DATA_HANDLE h) { (void)h; return EAS_FALSE; }
void *EAS_HWRegisterSignalHandler(void) { return NULL; }
EAS_RESULT EAS_HWUnRegisterSignalHandler(void *c) { (void)c; return EAS_SUCCESS; }

/* reports: dropped */
void EAS_Report(int severity, const char *fmt, ...) { (void)severity; (void)fmt; }
void EAS_ReportEx(int severity, unsigned long hashCode, int serialNum, ...) { (void)severity; (void)hashCode; (void)serialNum; }
