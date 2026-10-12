/* SPDX-License-Identifier: GPL-3.0-only */
/* Where Polly's update loader may erase / program: one definition for the loader's flash driver (loader.c ->
 * fm1_flash.h FL_RANGE_OK), its own checks (ldr_core.c) and the host test (tests/ldr_test.c).
 *
 * Felucca 1.5.1 (github.com/hugelton/Felucca, GPL-3.0) keeps its loader out of Felucca's store, 0x97000..0xE0000.
 * Polly keeps it out of more: everything above the app but the update's own staging is user data or the QY70
 * style bank (plat.h, qybank.h):
 *   songs 0x97000, the 16 slots 0x99000..0xB8FFF, the panel 0xDC000, the project 0xFC000, the QY70 bank
 *   0x93000..0x96FFF, 0xB9000..0xDBFFF, 0xDE000..0xDFFFF, 0xE5000..0xFBFFF.
 * So, [lo, hi):
 *   0x4000..0x93000   the app area: the package is written here and nowhere else (the flash head below it, the
 *                     SPL and isd_config, never)
 *   0xE0000..0xE5000  the update's staging (ota.c OTA_AREA: the staged loader, Polly's update record at 0xE4F00;
 *                     backup.c's restore journal RS_REC 0xE1000 lives here too, unused but outside an update):
 *                     only erased, to clear the record that started this loader
 *   0xE8000..0xE9000  one sector: the stock firmware's update record (0xE8F00), there when the stock firmware
 *                     staged this loader (a first install from it). Inside the bank's last region, but no bank
 *                     sector can look like a record (qybank.h: bytes 0xF00..0xF07 of every bank sector stay
 *                     0xFF, the record's "AT" is at 0xF06), so it is erased only when it is not the bank's
 * Restoring the stock firmware does not come here: the running app (ota.c) stages M-VAVE's own loader, which
 * writes the whole stock flash.bin itself (head and all). */
#pragma once
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI 0x93000u
#define LDR_STAGE_LO 0xE0000u
#define LDR_STAGE_HI 0xE5000u
#define LDR_STOCK_REC 0xE8000u                  /* the sector whose last 256 bytes hold the stock firmware's record */
/* [off, off + n) inside [lo, hi), without wrapping (fm1_flash.h FL_IN, here for the host test too) */
#define LDR_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                                (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define LDR_WRITE_OK(off, n) (LDR_IN(off, n, LDR_APP_LO, LDR_APP_HI) || LDR_IN(off, n, LDR_STAGE_LO, LDR_STAGE_HI) || \
                              LDR_IN(off, n, LDR_STOCK_REC, LDR_STOCK_REC + 0x1000u))
