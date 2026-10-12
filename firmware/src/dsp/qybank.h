/* SPDX-License-Identifier: GPL-3.0-only */
/* The QY70 style bank on the FM-1's flash (bring your own ROM: tools/qy70_install.py builds it from your QY70 ROM
 * and writes it over USB; nothing of Yamaha's is in this firmware). docs/qy70-import.md has the format.
 *
 * Where it lives: the flash Polly's firmware, store and the update loader leave free, four regions, 64 sectors:
 *   0x93000..0x96FFF, 0xB9000..0xDBFFF, 0xDE000..0xDFFFF, 0xE5000..0xFBFFF
 * The sector at QB_DIR (0xDE000) is the directory; the other 63 are the data, one logical byte stream in the
 * order above. Bytes 0xF00..0xF07 of every bank sector stay 0xFF: at the end of a firmware update the loader
 * erases any sector in 0x93000..0xFBFFF whose last 256 bytes look like an update record ("AT" at bytes 6..7),
 * so the logical stream steps over them (4088 bytes a sector).
 *
 * The directory (little-endian; written last, so a bank cut off while it was written has none):
 *   0  "PQB1"  4  version (QB_VERSION), styles (u16)  8  the codec's magic ("QYB3", tools/qy70_codec.py)
 *   12 data bytes used (u32)  16 the priors' logical offset (u32)  20 their bytes (u16)  22 rsv (u16)
 *   24 the priors' CRC-32  28 rsv (u32)  32 the styles, 24 bytes each:
 *      name (8, space padded), record offset (u32, logical), header bytes (u16), stream bytes (u16),
 *      CRC-32 of the record (header then stream), tempo x 10 (u16), beats a bar (u8), rsv (u8)
 *   then the CRC-32 of everything before it (zlib's).
 * A style's record: its header (qy70_codec.py head(): name, tempo, beats, bars a section, its tracks' sounds and
 * mixer, the phrase each track plays in each section) and its range-coded stream (qydec.h). */
#pragma once
#include <stdint.h>
#include "qydec.h"

#define QB_DIR 0x000DE000u
#define QB_SECTOR 4096u
#define QB_GUARD 0xF00u                  /* the guard bytes' place in a sector */
#define QB_GUARD_N 8u
#define QB_SECT_DATA (QB_SECTOR - QB_GUARD_N)
/* the regions (flash offsets, [lo, hi)); QB_DIR is the sector at the start of region 2 */
#define QB_R0_LO 0x00093000u
#define QB_R0_HI 0x00097000u
#define QB_R1_LO 0x000B9000u
#define QB_R1_HI 0x000DC000u
#define QB_R2_LO 0x000DE000u
#define QB_R2_HI 0x000E0000u
#define QB_R3_LO 0x000E5000u
#define QB_R3_HI 0x000FC000u
#define QB_NSECT ((QB_R0_HI - QB_R0_LO + QB_R1_HI - QB_R1_LO + QB_R2_HI - QB_R2_LO + QB_R3_HI - QB_R3_LO) / QB_SECTOR - 1u)
#define QB_CAPACITY (QB_NSECT * QB_SECT_DATA)   /* the data's logical bytes: 63 x 4088 = 257,544 */
/* a flash range inside the bank's regions (the directory sector included) */
#define QB_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define QB_FLASH_OK(off, n) (QB_IN(off, n, QB_R0_LO, QB_R0_HI) || QB_IN(off, n, QB_R1_LO, QB_R1_HI) || \
                             QB_IN(off, n, QB_R2_LO, QB_R2_HI) || QB_IN(off, n, QB_R3_LO, QB_R3_HI))

#define QB_MAGIC 0x31425150u             /* "PQB1" */
#define QB_VERSION 1u
#define QB_CODEC 0x33425951u             /* "QYB3" (a bank of another codec: QB_OLD, install it again) */
#define QB_MAXSTYLES 128
#define QB_DIR_HEAD 32u
#define QB_DIR_ENTRY 24u
#define QB_DIR_BYTES(n) (QB_DIR_HEAD + QB_DIR_ENTRY * (uint32_t)(n) + 4u)
#define QB_PRIORS ((QY_NPRIOR + 1) / 2)  /* the priors' bytes (4 bits each) */
#define QB_RECMAX 8192u                  /* a style's record (header and stream), at most */
#define QB_MAXN 2560                     /* a decoded style's notes (the ROM's largest: 2,259) */
#define QB_MAXC 4096                     /* its controller events (1,585; room for more kinds) */
#define QB_MAXPH 64                      /* its phrases (46) */
_Static_assert(QB_DIR_BYTES(QB_MAXSTYLES) <= QB_GUARD, "the directory ends before the sector's guard bytes");

/* flash sector i (0..QB_NSECT-1) of the data, and a logical offset's flash offset (QB_CAPACITY: none, 0) */
uint32_t qb_sector(uint32_t i);
uint32_t qb_phys(uint32_t logical);

/* a decoded style, in RAM while it plays (the app keeps two: one playing, one being decoded) */
typedef struct {
    uint8_t chan, drum, prog, vol, pan, rev, cho, vscale;   /* chan 0..15; vscale: velocity x vscale / 128 */
} qb_trk_t;
typedef struct {
    int16_t index;                       /* the bank's style number, -1 none */
    uint16_t tempo10;
    char name[9];
    uint8_t beats;                       /* a bar: 2..7 */
    uint8_t tmask;                       /* the tracks that play (bit t) */
    uint8_t nph;
    uint8_t bars[6];                     /* each section's bars, 1..8 */
    uint8_t use[6][8];                   /* section x track: its phrase, 0xFF none */
    uint8_t prog[6][8];                  /* the program it plays (the track's, or the phrase's own) */
    qb_trk_t trk[8];
    uint16_t nnotes, nctl;
    qy_phrase_t ph[QB_MAXPH];
    qy_note_t note[QB_MAXN];
    qy_ctl_t ctl[QB_MAXC];
} qb_style_t;

/* the bank: QB_NONE (no directory), QB_BAD (a damaged one), QB_OLD (another version or codec), else its styles */
enum { QB_NONE = 0, QB_BAD = -1, QB_OLD = -2 };
int qb_mount(void);                      /* main loop, at boot and after a bank is written */
void qb_unmount(void);                   /* as no bank (a test session: as a fresh FM-1) */
int qb_status(void);                     /* what qb_mount found */
int qb_count(void);                      /* the styles (0 unless mounted) */
const char *qb_name(int i);              /* "Bossa"; "" for none */
int qb_tempo(int i);                     /* bpm, 40..240 */
/* style i decoded into *out (main loop; its own working memory, static). 0, or < 0: QB_E_* */
enum { QB_E_INDEX = -1, QB_E_READ = -2, QB_E_CRC = -3, QB_E_HEAD = -4, QB_E_DECODE = -5 };
int qb_decode(int i, qb_style_t *out);
uint32_t qb_crc32(uint32_t c, const void *p, uint32_t n);   /* zlib's, c = 0 to start */

/* provided by the platform: read n bytes of flash at off (0 = ok) */
int qb_flash_read(uint32_t off, void *dst, uint32_t n);
