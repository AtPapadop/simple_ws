/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005, Jouni Malinen
 *
 * Licensed under the GNU General Public License v2 or BSD license.
 */

#include <stdlib.h>
#include <string.h>
#include "base64.h"

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *src, size_t len, char *out, size_t *out_len)
{
    size_t olen = ((len + 2) / 3) * 4 + 1;

    if (*out_len < olen)
    {
        return NULL;
    }

    char *pos = out;
    const unsigned char *end = src + len;
    const unsigned char *in = src;

    while (end - in >= 3)
    {
        *pos++ = base64_table[in[0] >> 2];
        *pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *pos++ = base64_table[((in[1] & 0x0F) << 2) | (in[2] >> 6)];
        *pos++ = base64_table[in[2] & 0x3F];
        in += 3;
    }

    if (end - in)
    {
        *pos++ = base64_table[in[0] >> 2];
        if (end - in == 1)
        {
            *pos++ = base64_table[(in[0] & 0x03) << 4];
            *pos++ = '=';
            *pos++ = '=';
        }
        else
        {
            *pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *pos++ = base64_table[(in[1] & 0x0F) << 2];
            *pos++ = '=';
        }
    }

    *pos = '\0';
    *out_len = pos - out;
    return out;
}

unsigned char *base64_decode(const unsigned char *src, size_t len, size_t *out_len)
{
    if (len % 4 != 0)
    {
        return NULL;
    }

    size_t padding = 0;
    if (len >= 2 && src[len - 1] == '=')
        padding++;
    if (len >= 2 && src[len - 2] == '=')
        padding++;

    size_t decoded_len = (len / 4) * 3 - padding;
    unsigned char *out = (unsigned char *)malloc(decoded_len + 1);
    if (!out)
        return NULL;

    unsigned char *pos = out;

    for (size_t i = 0; i < len; i += 4)
    {
        unsigned char b[4];

        for (int j = 0; j < 4; j++)
        {
            if (src[i + j] == '=')
            {
                b[j] = 0;
            }
            else
            {
                char *p = strchr(base64_table, src[i + j]);
                if (!p)
                {
                    free(out);
                    return NULL;
                }
                b[j] = p - base64_table;
            }
        }

        *pos++ = (b[0] << 2) | (b[1] >> 4);
        if (src[i + 2] != '=')
            *pos++ = (b[1] << 4) | (b[2] >> 2);
        if (src[i + 3] != '=')
            *pos++ = (b[2] << 6) | b[3];
    }

    *pos = '\0';
    if (out_len)
        *out_len = decoded_len;
    return out;
}
