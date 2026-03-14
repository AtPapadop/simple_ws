/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef __SIMPLE_WS_BASE64_H
#define __SIMPLE_WS_BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode binary data into Base64 text.
 * @param src Input byte buffer to encode.
 * @param len Number of bytes in src.
 * @param out Output character buffer that receives encoded text.
 * @param out_len Input: capacity of out. Output: encoded length written.
 * @return Pointer to out on success, or NULL on failure.
 */
char * base64_encode(const unsigned char *src, size_t len,
                              char *out, size_t *out_len);

/**
 * @brief Decode Base64 text into binary data.
 * @param src Input Base64 character buffer.
 * @param len Number of bytes in src.
 * @param out_len Output decoded byte length.
 * @return Newly allocated decoded buffer on success, or NULL on failure.
 */
unsigned char * base64_decode(const unsigned char *src, size_t len,
			      size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_BASE64_H */
