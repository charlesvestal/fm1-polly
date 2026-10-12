/* SPDX-License-Identifier: GPL-3.0-only */
/* A test session (tools/polly_remote.py run --fresh; mirror.c's remote, op 5; app_test in ui.c): Polly as a fresh boot
 * of a blank project, the flash untouched. While it is on, every object but the panel's calibration is loaded from and
 * saved to RAM (ts_pool), never the flash: at its start none (so everything loads its defaults: the project, the
 * songs, the 16 slots empty), and what the session saves (SAVE, the autosave, a slot taken) kept there, so it loads
 * back as from the flash. Ending it (asked, mirroring off, a restore; a reboot clears RAM) drops all of it and loads
 * the user's own again. Objects are kept run-length coded (a pattern is mostly zeros past its notes); one that does
 * not fit is not saved (as a flash error: NOT SAVED). Part of the unity build (project.c includes it, after plat.h);
 * tests/host/teststore_test.c too. */
#define TS_POOL 12288u
static struct {
    uint8_t on;
    uint32_t used;                                   /* ts_pool's bytes: entries obj, len16 (raw), n16 (coded), code */
} ts;
static uint8_t ts_pool[TS_POOL];

/* the entry of obj in ts_pool: its offset, -1 none */
static int32_t ts_find(uint32_t obj)
{
    for (uint32_t o = 0; o + 5u <= ts.used; o += 5u + (ts_pool[o + 3] | ts_pool[o + 4] << 8))
        if (ts_pool[o] == obj)
            return (int32_t)o;
    return -1;
}
/* PackBits-like: c < 128: c + 1 bytes as they are; c >= 128: the next byte c - 125 times (3..130). The code's
 * length, 0 when it would pass max */
static uint32_t ts_code(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t max)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t r = 1, l = 0;
        while (i + r < n && r < 130u && s[i + r] == s[i])
            r++;
        if (r >= 3u) {
            if (o + 2u > max)
                return 0;
            d[o++] = (uint8_t)(125u + r);
            d[o++] = s[i];
            i += r;
            continue;
        }
        while (i + l < n && l < 128u && !(i + l + 2u < n && s[i + l] == s[i + l + 1u] && s[i + l] == s[i + l + 2u]))
            l++;
        if (o + 1u + l > max)
            return 0;
        d[o++] = (uint8_t)(l - 1u);
        for (uint32_t k = 0; k < l; k++)
            d[o++] = s[i + k];
        i += l;
    }
    return o;
}
static int ts_load(uint32_t obj, void *dst, uint32_t max)
{
    int32_t e = ts_find(obj);
    uint32_t len, n, i = 0, o = 0;
    const uint8_t *c;
    uint8_t *d = dst;
    if (e < 0)
        return -1;
    len = ts_pool[e + 1] | ts_pool[e + 2] << 8;
    n = ts_pool[e + 3] | ts_pool[e + 4] << 8;
    c = ts_pool + e + 5;
    if (!len)
        return -1;                                   /* (an empty object: none, as storage.c's st_load) */
    if (len > max)
        len = max;
    while (i < n && o < len) {
        uint32_t k = c[i++];
        if (k >= 128u) {
            for (k -= 125u; k-- && o < len;)
                d[o++] = c[i];
            i++;
        } else
            for (k++; k-- && i < n;) {
                if (o < len)
                    d[o++] = c[i];
                i++;
            }
    }
    return (int)len;
}
static int ts_save(uint32_t obj, const void *src, uint32_t len)
{
    int32_t e;
    uint32_t n, sz;
    if (len > PLAT_STORE_MAX || ts.used + 5u > TS_POOL)
        return -1;
    n = len ? ts_code(src, len, ts_pool + ts.used + 5u, TS_POOL - ts.used - 5u) : 0u;
    if (len && !n)
        return -1;                                   /* no room: the old one kept */
    ts_pool[ts.used] = (uint8_t)obj;
    ts_pool[ts.used + 1] = (uint8_t)len;
    ts_pool[ts.used + 2] = (uint8_t)(len >> 8);
    ts_pool[ts.used + 3] = (uint8_t)n;
    ts_pool[ts.used + 4] = (uint8_t)(n >> 8);
    e = ts_find(obj);                                /* the old one (the new is past ts.used yet) */
    ts.used += 5u + n;
    if (e >= 0) {
        sz = 5u + (ts_pool[e + 3] | ts_pool[e + 4] << 8);
        for (uint32_t i = (uint32_t)e; i + sz < ts.used; i++)
            ts_pool[i] = ts_pool[i + sz];       /* (the ones after it down: forward, so overlapping is fine) */
        ts.used -= sz;
    }
    return 0;
}
static inline void ts_set(int on)
{
    ts.on = (uint8_t)(on != 0);
    ts.used = 0;
}
