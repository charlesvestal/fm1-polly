/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's own exports for its browser page (app/web/emu/index.html), beside fm1-base's (base/web/emu/emu_web.c, which
 * includes this: app.h APP_WEB).
 *
 * The QY70 style bank (bring your own ROM): the page builds the image from the tester's ROM (web/emu/qy_build.py, the
 * codec's --image) and keeps it in its browser; here it goes into the emulated flash as the simulator's `bank FILE`
 * writes it (the directory last). Before web_boot: the UI mounts it at its start; after: mounted again at once.
 * n = 0 erases the bank (Polly's own styles only). Returns the styles mounted, or qb_status()'s error (<= 0). */
static uint8_t bank_img[320u * 1024u];
__attribute__((used, visibility("default"))) uint8_t *web_bank_buf(void) { return bank_img; }
__attribute__((used, visibility("default"))) uint32_t web_bank_max(void) { return sizeof bank_img; }
__attribute__((used, visibility("default"))) int web_bank_load(uint32_t n)
{
    if (n > sizeof bank_img)
        n = 0;
    plat_data_erase(QB_DIR);                         /* (the flash erased first) */
    for (uint32_t i = 0; i < QB_NSECT; i++)
        plat_data_erase(qb_sector(i));
    for (uint32_t i = QB_SECTOR; i < n; i++) {
        uint32_t l = i - QB_SECTOR;
        if (l >= QB_CAPACITY)
            break;
        plat_data_write(qb_phys(l), &bank_img[i], 1);
    }
    if (n >= QB_SECTOR)
        plat_data_write(QB_DIR, bank_img, QB_SECTOR);   /* the directory last */
    if (started) {                                   /* (booted: the songs given to the arranger) */
        app_bank_changed();
        run_ms(1);
    } else
        qb_mount();
    return qb_status();
}
__attribute__((used, visibility("default"))) int web_bank_status(void) { return qb_status(); }
/* the styles the Style knob reaches: Polly's own, then the bank's */
__attribute__((used, visibility("default"))) int web_styles_own(void) { return arr_styles(); }
__attribute__((used, visibility("default"))) int web_styles_bank(void) { return qb_count(); }
/* the tests' check of a written bank: every style decodes (0), else the first failure's QB_E_* */
__attribute__((used, visibility("default"))) int web_bank_check(void)
{
    static qb_style_t s;
    for (int i = 0; i < qb_count(); i++) {
        int r = qb_decode(i, &s);
        if (r < 0)
            return r;
    }
    return 0;
}
