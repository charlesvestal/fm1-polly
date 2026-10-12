/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen and panel mirroring over USB-MIDI (web/mirror/index.html shows it, tools/polly_mirror.py saves a PNG), and,
 * for testing, the panel played from the host while it mirrors (tools/polly_remote.py). Off until a host asks; it costs
 * nothing then but a test in lcd_blit / lcd_fill and in plat_buttons / plat_keys / plat_enc / plat_master.
 *
 * Every frame goes in the update's SysEx framing (ota.c): F0 pack7(00 59 cmd len24 body ~sum8(body)) F7.
 *   host -> device  cmd 0x50, body 1 byte: 0 off, 1 on (also the keep-alive: at least every MR_TIMEOUT_MS, or
 *                   it goes off), 2 refresh (on, and the whole screen sent again). Each is answered:
 *   device -> host  cmd 0x50 hello: version 1, on (0 / 1), the screen's width - 1, height - 1 (239, 239)
 *                   cmd 0x51 rect: x, y, w (1..240: 0 means 256, never sent), h, seq; then the pixels of the w x h
 *                        rectangle row by row, run-length coded on RGB565 (below)
 *                   cmd 0x52 fill: x, y, w, h, seq, the colour (RGB565, high byte first)
 *                   cmd 0x53 input: buttons held u16 (bit B_*: plat.h), keys held u32 (bit k = key k, F3 = 0),
 *                        the encoders' detents since the last report, int8 each (SELECT, ALGORITHM, PRESETS,
 *                        KNOB 1..4: EN_*), MASTER u16 (plat_master: gain 0..4096), the lit buttons u16 (bit 15: PLAY
 *                        lit red), the lit keys u32 (little-endian, 21 bytes)
 *   host -> device  cmd 0x54 remote, body 5 bytes: seq, op, a, b, c; only while mirroring (else refused), one at a
 *                   time (the device keeps one frame from the host: send the next after the answer)
 *                     op 1 a button: a = B_* (plat.h), b = 1 down, 0 up
 *                     op 2 a key: a = 0..26 (F3 = 0), b = 1 down, 0 up
 *                     op 3 a turn: a = EN_*, b = detents (int8, + clockwise), added to the ones not yet taken
 *                     op 4 MASTER: a | b << 8 = gain 0..4096; 0xFFFF: the pot's again
 *                     op 0 let go: every button and key up, the turns dropped, MASTER the pot's
 *                   They go in at the plat_* calls (plat_buttons, plat_keys, plat_enc, plat_master: held = the panel's
 *                   OR the host's), the path the panel takes into the UI; the input reports show them. Mirroring off
 *                   (asked, timed out, unplugged, another host): everything let go. Never in the update mode's OCT- +
 *                   OCT+ (main_fm1.c reads the matrix), the calibration, or a restore's question (main.c: the hand's).
 *   device -> host  cmd 0x54: seq, op, 1 done / 0 refused (mirroring off, a bad op). The same seq again (an answer
 *                   lost, the host asks again): answered again, not done twice
 * seq counts rect and fill frames (mod 256): a gap tells the viewer one went missing (it asks for a refresh).
 *
 * The pixel code (a rect's body after its 5 bytes; state per frame: the colour now 0x0000, a 64-entry cache of
 * colours all 0x0000):
 *   0nnnnnnn          n + 1 more pixels of the colour now (1..128)
 *   10iiiiii          the colour now = cache[i]; one pixel of it
 *   11nnnnnn hi lo    the colour now = hi << 8 | lo, put in cache[MR_HASH(colour)]; n + 1 pixels of it (1..64)
 *   MR_HASH(c) = (c * 40503) >> 10 & 63 (32-bit unsigned)
 * The UI is flat colour with anti-aliased text: a few hundred bytes for most bands (tests/host/mirror_test.c).
 *
 * Bandwidth first for the music: the frames go to their own ring (usb.c mr_q, MRQ bytes), sent after the SysEx
 * ring (updates, backups, this file's input reports) and after the notes and clock, whole (nothing in the middle
 * of a SysEx message). A rectangle that does not fit in the ring now is dropped and its rows marked dirty; the
 * screen has no framebuffer to send them from later, so the UI is asked to draw everything again (ui.sig: each
 * band then redrawn whole), and what the viewer already has is skipped: a 32-bit hash of each 30-pixel segment of
 * each full-width row sent is kept; a row drawn again sends only its segments that changed (a playhead moving: a
 * narrow rect, not the band). So a busy screen on a slow link loses frames, never the picture.
 *
 * Input reports go in the SysEx ring (before the screen: the panel feels live), when something changed, 50 a
 * second at most (encoder turns add up meanwhile), and every MR_INPUT_IDLE_MS anyway.
 *
 * Cost: in the main loop only (lcd_blit / lcd_fill callers and mirror_step), never in the audio interrupt.
 *
 * Hooks (main.c on the device, host/polly_host.c, tests/host/mirror_test.c):
 *   mr_free()         bytes free in the frame ring
 *   mr_put(p, n)      one whole F0..F7 frame into the frame ring (it fits: mr_free checked)
 *   mr_link()         the USB connection: -1 none (not configured); else a number that changes with each new one
 *   mr_busy()         nothing to send now (the bus suspended, a backup streaming): rects dropped (dirty), no input
 *   mr_ctl_send(p, n) one whole F0..F7 frame into the SysEx ring if it fits now: 0, else -1 (nothing sent)
 *   mr_redraw()       the UI draws every band again at its next frame
 * and plat_ms, plat_buttons, plat_keys, plat_master (plat.h). The flag mirror_on is the includer's (lcd.c). */
#define MR_REQ 0x50u
#define MR_RECT 0x51u
#define MR_FILL 0x52u
#define MR_INPUT 0x53u
#define MR_REMOTE 0x54u
#define MR_VERSION 2u                                 /* 2: the remote (cmd 0x54) */
#define MR_W 240u
#define MR_H 240u
#define MR_CAP 256u                                   /* a rect frame's pixel code, about (one row more at most) */
#define MR_ROWMAX (3u * MR_W)                         /* a row's code at worst (every pixel a new colour) */
#define MR_TIMEOUT_MS 6000u                           /* no request from the host this long: off */
#define MR_INPUT_MS 20u                               /* input reports: 50 a second at most */
#define MR_INPUT_IDLE_MS 500u                         /* and one this often anyway */
#define MR_REDRAW_MS 50u                              /* dirty rows: the UI asked to draw again this often at most, */
#define MR_REDRAW_ROOM (MRQ * 3u / 4u)                /* once the ring has this much room */
#define MR_HASH(c) ((((uint32_t)(c)) * 40503u) >> 10 & 63u)
#define MR_SEGW 30u                                   /* a row's segments: only the changed ones are sent */
#define MR_NSEG (MR_W / MR_SEGW)
#define MR_MROWS 32u                                  /* rows a rect frame takes at most */
#define MR_NIN 21u

static uint32_t mr_free(void);
static void mr_put(const uint8_t *p, uint32_t n);
static int mr_link(void);
static int mr_busy(void);
static int mr_ctl_send(const uint8_t *p, uint32_t n);
static void mr_redraw(void);

static struct {
    uint8_t seq, ask_redraw;
    int link;
    uint32_t heard, redraw_t, input_t;
    uint32_t known[(MR_H + 31u) / 32u];               /* the viewer's row r: its segments' hashes are mr_seg[r] */
    uint32_t dirty[(MR_H + 31u) / 32u];               /* the viewer's row r is wrong somewhere (dropped) */
    int16_t enc[7];                                   /* detents not reported yet (EN_*) */
    uint16_t lit_b;
    uint32_t lit_k;
    uint8_t last[MR_NIN];                             /* the last input report's body */
    uint16_t cache[64], cur;
    uint32_t pos;                                     /* the frame being coded: its length in mr_msg */
    /* counters (tests, the host's report) */
    uint32_t bytes, frames, rects, rows, drops, skipped, redraws, inputs, px;
} mr;
static struct {                                      /* the host's hands (cmd 0x54) */
    uint16_t btn;                                     /* held: bit B_* */
    uint32_t keys;                                    /* held: bit k */
    int8_t enc[7];                                    /* detents not taken yet (EN_*) */
    uint16_t master;                                  /* MASTER + 1; 0: the pot's */
    uint8_t seq, res, have;                           /* the last request's seq and answer (have: one since on) */
    uint32_t ops;                                     /* requests done (tests) */
} mr_in;
static uint32_t mr_seg[MR_H][MR_NSEG] EN_POOL;        /* what the viewer has: each row's segments' hashes */
static uint32_t mr_mh[MR_MROWS][MR_NSEG];             /* the frame being coded: its rows' new hashes */
static uint8_t mr_msg[6 + 5 + MR_CAP + MR_ROWMAX + 1] EN_POOL;
static uint8_t mr_wire[2 + (sizeof mr_msg * 8u + 6u) / 7u] EN_POOL;

static int mr_bit(const uint32_t *m, uint32_t r) { return (int)(m[r >> 5] >> (r & 31u) & 1u); }
static void mr_set(uint32_t *m, uint32_t r, int on)
{
    if (on)
        m[r >> 5] |= 1u << (r & 31u);
    else
        m[r >> 5] &= ~(1u << (r & 31u));
}

static uint32_t mr_pack7(const uint8_t *in, uint32_t n, uint8_t *out)   /* 8-to-7, LSB first (ota.c's) */
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8u;
        while (nb >= 7u) {
            out[o++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7u;
        }
    }
    if (nb)
        out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}

/* mr_msg[6 ..] holds a body of n bytes: the header, the sum, packed into mr_wire; the wire length */
static uint32_t mr_seal(uint32_t cmd, uint32_t n)
{
    uint32_t i, s = 0, w;
    mr_msg[0] = 0;
    mr_msg[1] = 0x59;
    mr_msg[2] = (uint8_t)cmd;
    mr_msg[3] = (uint8_t)n;
    mr_msg[4] = (uint8_t)(n >> 8);
    mr_msg[5] = (uint8_t)(n >> 16);
    for (i = 0; i < n; i++)
        s += mr_msg[6 + i];
    mr_msg[6 + n] = (uint8_t)~s;
    mr_wire[0] = 0xF0;
    w = 1u + mr_pack7(mr_msg, 7u + n, mr_wire + 1);
    mr_wire[w++] = 0xF7;
    return w;
}

static uint32_t mr_hash(const uint16_t *p, uint32_t w)   /* FNV-1a over the pixels */
{
    uint32_t h = 2166136261u;
    while (w--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static void mr_op(uint32_t b) { mr_msg[mr.pos++] = (uint8_t)b; }

/* one row's pixels (as stored: RGB565 byte-swapped, the panel's order) coded onto the frame */
static void mr_code_row(const uint16_t *px, uint32_t w)
{
    uint32_t i = 0;
    while (i < w) {
        uint32_t v = px[i], n = 1, c, k, m;
        while (i + n < w && px[i + n] == v)
            n++;
        i += n;
        c = (v >> 8 | v << 8) & 0xFFFFu;
        if (c != mr.cur) {
            k = MR_HASH(c);
            if (mr.cache[k] == c) {
                mr_op(0x80u | k);
                n--;
            } else {
                m = n > 64u ? 64u : n;
                mr_op(0xC0u | (m - 1u));
                mr_op(c >> 8);
                mr_op(c & 0xFFu);
                mr.cache[k] = (uint16_t)c;
                n -= m;
            }
            mr.cur = (uint16_t)c;
        }
        while (n) {
            m = n > 128u ? 128u : n;
            mr_op(m - 1u);
            n -= m;
        }
    }
}

static void mr_rows_dirty(uint32_t y, uint32_t h)
{
    for (uint32_t r = y; r < y + h && r < MR_H; r++) {
        mr_set(mr.dirty, r, 1);
        mr_set(mr.known, r, 0);
    }
}

/* the frame in mr_wire (w bytes) to the ring: 1 sent, 0 no room */
static int mr_send(uint32_t w)
{
    if (mr_busy() || mr_free() < w)
        return 0;
    mr_put(mr_wire, w);
    mr.bytes += w;
    mr.frames++;
    mr.seq++;
    return 1;
}

/* row r (y + r) of a blit: its segments' hashes into sh, the changed ones' span s0..s1 (segments); 0: unchanged */
static int mr_span(const uint16_t *row, uint32_t yr, int full, uint32_t *sh, uint32_t *s0, uint32_t *s1)
{
    uint32_t i, known = full && mr_bit(mr.known, yr);
    *s0 = MR_NSEG;
    *s1 = 0;
    if (!full)
        return 1;                                     /* (a part of rows: sent whole, its rows then unknown) */
    for (i = 0; i < MR_NSEG; i++) {
        sh[i] = mr_hash(row + i * MR_SEGW, MR_SEGW);
        if (!known || sh[i] != mr_seg[yr][i]) {
            if (*s0 == MR_NSEG)
                *s0 = i;
            *s1 = i;
        }
    }
    return *s0 < MR_NSEG;
}

/* lcd_blit's copy: rows y .. y+h-1 of w pixels from x (the panel's byte order). Full-width rows: only the segments
 * that changed since the viewer had them (a frame: rows whose changes fall in the same span) */
static void mirror_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t r = 0, sh[MR_NSEG], s0, s1, have = 0, ch = 0;
    int full = x == 0 && w == MR_W, drop = 0;
    if (!w || !h || x >= MR_W || y >= MR_H || w > MR_W)
        return;
    if (y + h > MR_H)
        h = MR_H - y;
    mr.px += w * h;
    while (r < h) {
        uint32_t r0, rows = 0, m0, m1, fx, fw;
        if (!have) {
            ch = (uint32_t)mr_span(px + r * w, y + r, full, sh, &s0, &s1);
            have = 1;
        }
        if (!ch || drop) {                            /* the viewer has it; or the ring is full: dirty */
            if (ch)
                mr_rows_dirty(y + r, 1);
            else
                mr.skipped++;
            r++;
            have = 0;
            continue;
        }
        r0 = r;
        m0 = s0;
        m1 = s1;
        fx = full ? m0 * MR_SEGW : x;
        fw = full ? (m1 + 1u - m0) * MR_SEGW : w;
        mr.pos = 11;                                  /* x y w h seq, then the code */
        mr.cur = 0;
        memset(mr.cache, 0, sizeof mr.cache);
        while (r < h && rows < MR_MROWS) {
            uint32_t at = mr.pos;
            if (!have) {
                ch = (uint32_t)mr_span(px + r * w, y + r, full, sh, &s0, &s1);
                have = 1;
            }
            if (rows && (!ch || (full && (s0 < m0 || s1 > m1))))
                break;                                /* unchanged, or changed elsewhere: the next frame's */
            mr_code_row(px + r * w + (full ? fx : 0u), fw);
            if (rows && mr.pos - 11u > MR_CAP) {
                mr.pos = at;                          /* (it starts the next frame) */
                break;
            }
            memcpy(mr_mh[rows], sh, sizeof sh);
            rows++;
            r++;
            have = 0;
        }
        mr_msg[6] = (uint8_t)fx;
        mr_msg[7] = (uint8_t)(y + r0);
        mr_msg[8] = (uint8_t)fw;
        mr_msg[9] = (uint8_t)rows;
        mr_msg[10] = mr.seq;
        if (mr_send(mr_seal(MR_RECT, mr.pos - 6u))) {
            mr.rects++;
            mr.rows += rows;
            for (uint32_t k = 0; k < rows; k++) {
                uint32_t yr = y + r0 + k;
                if (full) {                           /* (an unknown row's span is all of it) */
                    for (uint32_t i = m0; i <= m1; i++)
                        mr_seg[yr][i] = mr_mh[k][i];
                    mr_set(mr.known, yr, 1);
                    mr_set(mr.dirty, yr, 0);
                } else
                    mr_set(mr.known, yr, 0);
            }
        } else {
            mr.drops++;
            mr_rows_dirty(y + r0, rows);
            drop = 1;
        }
    }
}

/* lcd_fill's copy (clipped already) */
static void mirror_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t r, i, hs = 2166136261u, sw = (uint16_t)(c >> 8 | c << 8), full = x == 0 && w == MR_W;
    if (!w || !h || x >= MR_W || y >= MR_H)
        return;
    mr_msg[6] = (uint8_t)x;
    mr_msg[7] = (uint8_t)y;
    mr_msg[8] = (uint8_t)w;
    mr_msg[9] = (uint8_t)h;
    mr_msg[10] = mr.seq;
    mr_msg[11] = (uint8_t)(c >> 8);
    mr_msg[12] = (uint8_t)c;
    if (!mr_send(mr_seal(MR_FILL, 7))) {
        mr.drops++;
        mr_rows_dirty(y, h);
        return;
    }
    for (i = 0; i < MR_SEGW; i++)
        hs = (hs ^ sw) * 16777619u;
    for (r = y; r < y + h && r < MR_H; r++) {
        mr_set(mr.known, r, (int)full);
        if (full) {
            for (i = 0; i < MR_NSEG; i++)
                mr_seg[r][i] = hs;
            mr_set(mr.dirty, r, 0);
        }
    }
}

/* the encoders' turns and the lights, as the platform sees them (plat_enc, plat_leds) */
static void mirror_enc(int role, int32_t n)
{
    if (!mirror_on || role < 0 || role >= 7)
        return;
    n += mr.enc[role];
    mr.enc[role] = (int16_t)(n > 4000 ? 4000 : n < -4000 ? -4000 : n);
}
static void mirror_leds(uint32_t buttons, uint32_t keys)
{
    mr.lit_b = (uint16_t)buttons;
    mr.lit_k = keys;
}

static void mr_hello(void)
{
    mr_msg[6] = MR_VERSION;
    mr_msg[7] = mirror_on;
    mr_msg[8] = MR_W - 1u;
    mr_msg[9] = MR_H - 1u;
    mr_ctl_send(mr_wire, mr_seal(MR_REQ, 4));
}

static void mr_all_dirty(void)
{
    mr_rows_dirty(0, MR_H);
    mr.ask_redraw = 1;
    mr.redraw_t = plat_ms() - MR_REDRAW_MS;
}

static void mr_in_letgo(void)
{
    mr_in.btn = 0;
    mr_in.keys = 0;
    memset(mr_in.enc, 0, sizeof mr_in.enc);
    mr_in.master = 0;
}
static void mirror_off(void)
{
    mirror_on = 0;
    memset(mr.enc, 0, sizeof mr.enc);
    mr_in_letgo();                                    /* the host's buttons and keys up: the UI sees them let go */
    mr_in.have = 0;
}

/* the host's input, as the plat_* calls add it to the panel's */
static uint32_t mirror_in_buttons(void) { return mirror_on ? mr_in.btn : 0u; }
static uint32_t mirror_in_keys(void) { return mirror_on ? mr_in.keys : 0u; }
static int32_t mirror_in_enc(int role)
{
    int32_t n;
    if (!mirror_on || role < 0 || role >= 7)
        return 0;
    n = mr_in.enc[role];
    mr_in.enc[role] = 0;
    return n;
}
static uint32_t mirror_in_master(uint32_t pot) { return mirror_on && mr_in.master ? mr_in.master - 1u : pot; }

/* a remote request (cmd 0x54, its 5 bytes): done, answered */
static void mirror_remote(const uint8_t *b)
{
    uint32_t seq = b[0], op = b[1], a = b[2], v = b[3], ok = 0;
    int link = mr_link();
    if (mirror_on && link == mr.link) {
        mr.heard = plat_ms();                         /* (also a keep-alive) */
        if (mr_in.have && seq == mr_in.seq)
            ok = mr_in.res;                           /* asked again: the same answer, not done again */
        else {
            ok = 1;
            if (op == 0)
                mr_in_letgo();
            else if (op == 1 && a < (uint32_t)NB)
                mr_in.btn = (uint16_t)(v ? mr_in.btn | 1u << a : mr_in.btn & ~(1u << a));
            else if (op == 2 && a < (uint32_t)NKEYS)
                mr_in.keys = v ? mr_in.keys | 1u << a : mr_in.keys & ~(1u << a);
            else if (op == 3 && a < 7u) {
                int32_t n = mr_in.enc[a] + (int8_t)v;
                mr_in.enc[a] = (int8_t)(n > 127 ? 127 : n < -127 ? -127 : n);
            } else if (op == 4 && (a | v << 8) == 0xFFFFu)
                mr_in.master = 0;
            else if (op == 4 && (a | v << 8) <= 4096u)
                mr_in.master = (uint16_t)((a | v << 8) + 1u);
            else
                ok = 0;
            mr_in.seq = (uint8_t)seq;
            mr_in.res = (uint8_t)ok;
            mr_in.have = 1;
            mr_in.ops += ok;
        }
    }
    mr_msg[6] = (uint8_t)seq;
    mr_msg[7] = (uint8_t)op;
    mr_msg[8] = (uint8_t)ok;
    mr_ctl_send(mr_wire, mr_seal(MR_REMOTE, 3));      /* (lost: the host asks again) */
}

/* a request from the host (cmd 0x50, its body's byte) */
static void mirror_cmd(uint32_t op)
{
    int link = mr_link();
    if (link < 0)
        return;
    mr.heard = plat_ms();
    if (op == 0) {
        mirror_off();
    } else if (!mirror_on || op == 2u || link != mr.link) {
        if (mirror_on && link != mr.link)
            mirror_off();                             /* (another host: the first one's hands let go) */
        mirror_on = 1;
        mr.link = link;
        memset(mr.last, 0xFF, sizeof mr.last);        /* an input report at once */
        mr.input_t = plat_ms() - MR_INPUT_IDLE_MS;
        mr_all_dirty();
    }
    mr_hello();
}

static void mr_wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void mr_wr32(uint8_t *p, uint32_t v) { mr_wr16(p, v); mr_wr16(p + 2, v >> 16); }

/* the main loop, each pass: the input report, a redraw when rows are dirty, off when the host went away */
static void mirror_step(void)
{
    uint32_t now, i, any;
    uint8_t *b = mr_msg + 6;
    if (!mirror_on)
        return;
    now = plat_ms();
    if (mr_link() != mr.link || now - mr.heard > MR_TIMEOUT_MS) {
        mirror_off();                                 /* unplugged, another host, or the viewer gone */
        return;
    }
    if (mr_busy())
        return;
    any = 0;                                          /* rows dirty: the UI draws again once the ring has room */
    for (i = 0; i < sizeof mr.dirty / 4u; i++)
        any |= mr.dirty[i];
    if ((any || mr.ask_redraw) && now - mr.redraw_t >= MR_REDRAW_MS && mr_free() >= MR_REDRAW_ROOM) {
        mr.ask_redraw = 0;
        mr.redraw_t = now;
        mr.redraws++;
        mr_redraw();
    }
    if (now - mr.input_t < MR_INPUT_MS)
        return;
    mr_wr16(b, plat_buttons());
    mr_wr32(b + 2, plat_keys());
    for (i = 0, any = 0; i < 7u; i++) {
        int32_t d = mr.enc[i];
        b[6 + i] = (uint8_t)(int8_t)(d > 127 ? 127 : d < -127 ? -127 : d);
        any |= (uint32_t)d;
    }
    mr_wr16(b + 13, plat_master());
    mr_wr16(b + 15, mr.lit_b);
    mr_wr32(b + 17, mr.lit_k);
    {
        int32_t dm = (int32_t)(b[13] | b[14] << 8) - (int32_t)(mr.last[13] | mr.last[14] << 8);
        if (!any && now - mr.input_t < MR_INPUT_IDLE_MS && !memcmp(b, mr.last, 6) &&
            !memcmp(b + 15, mr.last + 15, 6) && dm < 32 && dm > -32)
            return;                                   /* nothing new (MASTER: moved by 32 / 4096 or more) */
    }
    if (mr_ctl_send(mr_wire, mr_seal(MR_INPUT, MR_NIN)))
        return;                                       /* the SysEx ring full: next pass */
    for (i = 0; i < 7u; i++)
        mr.enc[i] = (int16_t)(mr.enc[i] - (int8_t)b[6 + i]);
    memcpy(mr.last, b, MR_NIN);
    mr.input_t = now;
    mr.inputs++;
}
