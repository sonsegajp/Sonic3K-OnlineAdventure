/*
 * codec_kosinski.c — Kosinski and Kosinski-moduled decoders.
 *
 * Kosinski is an LZSS variant: a 16-bit little-endian descriptor field
 * supplies flag bits LSB-first, interleaved with the data it describes.
 *   1        -> literal byte
 *   0 0 b b  -> inline match, count 2..5, distance 1..256   (1 distance byte)
 *   0 1      -> full match, 2 distance/count bytes, optional 3rd count byte;
 *               a third byte of 0 terminates the stream.
 */
#include "s3r_codec.h"

typedef struct {
    const uint8_t *src;
    size_t         len;
    size_t         pos;
    uint16_t       desc;
    int            bits_left;
    int            overrun;
} KosBits;

static uint8_t kos_byte(KosBits *b)
{
    if (b->pos >= b->len) { b->overrun = 1; return 0; }
    return b->src[b->pos++];
}

/* Descriptor words are little-endian and consumed LSB-first. */
static int kos_bit(KosBits *b)
{
    int bit;
    if (b->bits_left == 0) {
        uint8_t lo = kos_byte(b);
        uint8_t hi = kos_byte(b);
        b->desc = (uint16_t)(lo | (hi << 8));
        b->bits_left = 16;
    }
    bit = b->desc & 1u;
    b->desc >>= 1;
    b->bits_left--;

    /* The 68000 decoder fetches the next descriptor immediately after it
     * consumes bit 16, before it reads any literal/match payload controlled by
     * that final bit.  Kosinski streams are laid out around that early fetch;
     * waiting until the next kos_bit() call mistakes the first two payload
     * bytes for a descriptor whenever an item straddles the boundary. */
    if (b->bits_left == 0) {
        uint8_t lo = kos_byte(b);
        uint8_t hi = kos_byte(b);
        b->desc = (uint16_t)(lo | (hi << 8));
        b->bits_left = 16;
    }
    return bit;
}

size_t s3r_kosinski_decode(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t dst_cap, size_t *consumed)
{
    KosBits b;
    size_t out = 0;

    if (!src || !dst) return 0;
    b.src = src; b.len = src_len; b.pos = 0;
    b.desc = 0; b.bits_left = 0; b.overrun = 0;

    for (;;) {
        if (b.overrun) return 0;

        if (kos_bit(&b)) {                       /* literal */
            if (out >= dst_cap) return 0;
            dst[out++] = kos_byte(&b);
            continue;
        }

        size_t count, distance;

        if (kos_bit(&b)) {                       /* full match */
            uint8_t lo = kos_byte(&b);
            uint8_t hi = kos_byte(&b);
            count    = (size_t)(hi & 0x07u);
            distance = 0x2000u - ((((size_t)hi & 0xF8u) << 5) | lo);
            if (count == 0) {
                uint8_t n = kos_byte(&b);
                if (n == 0) break;               /* end of stream */
                if (n == 1) continue;            /* module boundary / no-op */
                count = (size_t)n + 1;
            } else {
                count += 2;
            }
        } else {                                 /* inline match */
            int h = kos_bit(&b);
            int l = kos_bit(&b);
            count    = (size_t)(((h << 1) | l) + 2);
            distance = 0x100u - (size_t)kos_byte(&b);
        }

        if (b.overrun) return 0;
        if (distance == 0 || distance > out) return 0;   /* would read before start */
        if (out + count > dst_cap) return 0;

        for (size_t i = 0; i < count; i++) {
            dst[out] = dst[out - distance];
            out++;
        }
    }

    if (b.overrun) return 0;
    if (consumed) *consumed = b.pos;
    return out;
}

/*
 * Kosinski-moduled: a 2-byte big-endian decompressed size, then a run of
 * independent Kosinski streams each producing one 0x1000-byte module (the
 * last is the remainder). Modules are padded to a 0x10-byte boundary.
 */
size_t s3r_kosinskim_decode(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t dst_cap, size_t *consumed)
{
    size_t full, pos, out = 0;

    if (!src || !dst || src_len < 2) return 0;

    full = ((size_t)src[0] << 8) | src[1];
    if (full == 0xA000u) full = 0x8000u;     /* documented special case */
    if (full == 0 || full > dst_cap) return 0;

    pos = 2;
    while (out < full) {
        size_t want = full - out;
        size_t got, used = 0;
        if (want > 0x1000u) want = 0x1000u;

        if (pos >= src_len) return 0;
        got = s3r_kosinski_decode(src + pos, src_len - pos,
                                  dst + out, dst_cap - out, &used);
        if (got != want) return 0;           /* module must fill exactly */

        out += got;
        pos += used;
        pos = (pos + 0x0Fu) & ~(size_t)0x0Fu; /* align to next module */
    }

    if (consumed) *consumed = pos;
    return out;
}
