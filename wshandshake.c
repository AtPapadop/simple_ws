/* wshandshake.c - websocket lib
 *
 * Copyright (C) 2016 Borislav Sapundzhiev
 * Copyright (C) 2025 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wshandshake.h"

#ifdef _WIN32
#define snprintf(buf, len, format, ...) _snprintf_s(buf, len, len, format, __VA_ARGS__)
#define strdup _strdup
#endif

/* Reads a line from the HTTP request buffer */
static int http_header_readline(char *in, char *buf, size_t len)
{
    char *ptr = buf;
    char *ptr_end = buf + len - 1;

    while (ptr < ptr_end)
    {
        *ptr = *in++;
        if (*ptr == '\0')
            break;
        if (*ptr == '\r')
            continue;
        if (*ptr == '\n')
        {
            *ptr = '\0';
            return ptr - buf;
        }
        ptr++;
    }
    *ptr = '\0';
    return ptr - buf;
}

/* Parses individual HTTP headers */
static int http_parse_headers(http_header *header, char *hdr_line)
{
    char *p = strchr(hdr_line, ':');
    if (!p)
        return -1;

    *p = '\0'; // Split name and value
    char *header_content = p + 1;

    // Trim leading spaces
    while (*header_content == ' ')
        header_content++;

    if (strncmp(WS_HDR_UPG, hdr_line, strlen(WS_HDR_UPG)) == 0)
    {
        header->upgrade = (strncmp(WS_WEBSOCK, header_content, strlen(WS_WEBSOCK)) == 0);
    }
    else if (strncmp(WS_HDR_VER, hdr_line, strlen(WS_HDR_VER)) == 0)
    {
        header->version = (uint8_t)atoi(header_content);
    }
    else if (strncmp(WS_HDR_KEY, hdr_line, strlen(WS_HDR_KEY)) == 0)
    {
        strncpy(header->key, header_content, sizeof(header->key) - 1);
        header->key[sizeof(header->key) - 1] = '\0';
    }

    *p = ':'; // Restore separator
    return 0;
}

/* Parses the first request line (GET /path HTTP/1.1) */
static int http_parse_request_line(http_header *header, char *hdr_line)
{
    char *token = strtok(hdr_line, " ");
    if (token)
        strncpy(header->method, token, sizeof(header->method) - 1);

    token = strtok(NULL, " ");
    if (token)
        strncpy(header->uri, token, sizeof(header->uri) - 1);

    return 0;
}

/* Parses the entire WebSocket handshake request */
static void ws_http_parse_handshake_header(http_header *header, uint8_t *in_buf, size_t in_len)
{
    char header_line[256];
    int res;
    int count = 0;

    header->type = WS_ERROR_FRAME;

    /* We read lines until we run out of buffer (in_len) or get an empty line. */
    while (in_len > 0)
    {
        size_t read_limit = (in_len < sizeof(header_line) - 1) ? in_len : (sizeof(header_line) - 1);

        res = http_header_readline((char *)in_buf, header_line, read_limit);
        if (res <= 0)
            break;

        size_t consumed = res + 2;

        // If we might have read only '\n' (no '\r'), adjust if needed.
        // But let's keep it simple for a typical CRLF scenario.
        if (consumed > in_len)
            break;

        // Parse the line
        if (count == 0)
        {
            http_parse_request_line(header, header_line);
        }
        else
        {
            http_parse_headers(header, header_line);
        }
        count++;

        // Advance the buffer pointer
        in_buf += consumed;
        in_len -= consumed;
    }

    /* If we have a key AND the version is correct => mark as opening frame. */
    if (header->key[0] && header->version == WS_VERSION)
    {
        header->type = WS_OPENING_FRAME;
    }
}

/* Generates the WebSocket accept key */
int ws_make_accept_key(const char *key, char *out_key, size_t *out_len)
{
    uint8_t sha[SHA1HashSize];
    char buffer[128];

    size_t key_len = strlen(key);
    size_t length = key_len + strlen(WS_MAGIC);

    if (length >= sizeof(buffer))
        return 0;

    snprintf(buffer, sizeof(buffer), "%s%s", key, WS_MAGIC);
    SHA1(sha, buffer, length);
    base64_encode(sha, SHA1HashSize, out_key, out_len);

    return (int)*out_len;
}

/* Generates the WebSocket handshake response */
static void ws_get_handshake_header(http_header *header, uint8_t *out_buff, size_t *out_len)
{
    int written = 0;
    char new_key[64] = {0};
    size_t len = sizeof(new_key);

    if (header->type == WS_OPENING_FRAME)
    {
        ws_make_accept_key(header->key, new_key, &len);
        written = snprintf((char *)out_buff, *out_len,
                           "HTTP/1.1 101 Switching Protocols\r\n"
                           "%s: %s\r\n"
                           "%s: %s\r\n"
                           "%s: %s\r\n\r\n",
                           WS_HDR_UPG, WS_WEBSOCK,
                           WS_HDR_CON, WS_HDR_UPG,
                           WS_HDR_ACP, new_key);
    }
    else
    {
        written = snprintf((char *)out_buff, *out_len,
                           "HTTP/1.1 400 Bad Request\r\n"
                           "%s: %d\r\n\r\n"
                           "Bad request",
                           WS_HDR_VER, WS_VERSION);
    }

    assert(written < (int)*out_len);
    *out_len = written;
}

/* Handles the WebSocket handshake */
int ws_handshake(http_header *header, uint8_t *in_buf, size_t in_len, size_t *out_len)
{
    ws_http_parse_handshake_header(header, in_buf, in_len);
    ws_get_handshake_header(header, in_buf, out_len);
    return 0;
}

/* Generates the WebSocket handshake header for a client */
int ws_make_handshake(uint8_t *out_buff, size_t *out_len, const char *host, const char *key)
{
    int written = 0;

    if (*out_len < 256)
        return -1;
    written = snprintf((char *)out_buff, *out_len,
                       "GET /chat HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Key: %s\r\n"
                       "Sec-WebSocket-Version: 13\r\n"
                       "\r\n",
                       host, key);

    if (written < 0 || written >= (int)*out_len)
        return -1;
    *out_len = (size_t)written;
    return 0;
}
