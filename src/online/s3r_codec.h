/*
 * s3r_codec.h — decoders for the cartridge's compressed data formats.
 *
 * Sonic 3 & Knuckles stores art in Nemesis (tile art) and Kosinski /
 * Kosinski-moduled (large art, level layouts) streams. These are our own
 * implementations written from the format descriptions; they are validated
 * against VRAM the running recompilation produced (see tools/validate_art.py).
 *
 * Every decoder is bounds-checked and returns 0 on malformed input rather
 * than trusting the stream, because the extractor speculatively decodes
 * unknown cartridge regions while building the catalogue.
 */
#ifndef S3R_CODEC_H
#define S3R_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each returns the number of decompressed bytes written to `dst`, or 0 on
 * failure. `consumed` (optional) receives the number of source bytes read,
 * which the catalogue builder uses to size the region in the cartridge. */

size_t s3r_nemesis_decode  (const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t dst_cap, size_t *consumed);

size_t s3r_kosinski_decode (const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t dst_cap, size_t *consumed);

size_t s3r_kosinskim_decode(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t dst_cap, size_t *consumed);

size_t s3r_enigma_decode   (const uint8_t *src, size_t src_len,
                            uint16_t starting_tile,
                            uint8_t *dst, size_t dst_cap, size_t *consumed);

#ifdef __cplusplus
}
#endif
#endif /* S3R_CODEC_H */
