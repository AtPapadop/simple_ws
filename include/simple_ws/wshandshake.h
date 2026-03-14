/* wshandshake.h - websocket lib
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

#ifndef __SIMPLE_WS_WSHANDSHAKE_H
#define __SIMPLE_WS_WSHANDSHAKE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "websocket.h"

#define WS_HDR_KEY "Sec-WebSocket-Key"
#define WS_HDR_VER "Sec-WebSocket-Version"
#define WS_HDR_ACP "Sec-WebSocket-Accept"
#define WS_HDR_ORG "Origin"
#define WS_HDR_HST "Host"
#define WS_HDR_UPG "Upgrade"
#define WS_HDR_CON "Connection"

typedef struct
{
	char method[4];		 // HTTP method (GET)
	char uri[128];		 // Requested URI
	char key[32];			 // WebSocket key
	uint8_t version;	 // WebSocket version
	uint8_t upgrade;	 // Upgrade header flag
	uint8_t websocket; // WebSocket flag
	wsFrameType type;	 // Frame type
} http_header_t;

/**
 * @brief Parse and validate an incoming WebSocket opening handshake request.
 * @param header Output parsed HTTP/WebSocket header fields.
 * @param in_buf Input request bytes.
 * @param in_len Number of bytes in in_buf.
 * @param out_len Output number of bytes consumed/produced by the parser.
 * @return 0 on success, non-zero on parse or validation failure.
 */
int ws_handshake(http_header_t *header, uint8_t *in_buf, size_t in_len, size_t *out_len);

/**
 * @brief Create a WebSocket Sec-WebSocket-Accept value from a client key.
 * @param key Client-provided Sec-WebSocket-Key value.
 * @param out_key Output buffer for the generated accept key.
 * @param out_len Input: capacity of out_key. Output: key length written.
 * @return 0 on success, non-zero on failure.
 */
int ws_make_accept_key(const char *key, char *out_key, size_t *out_len);

/**
 * @brief Build the HTTP 101 Switching Protocols handshake response bytes.
 * @param out_buff Output buffer that receives the response message.
 * @param out_len Input: capacity of out_buff. Output: bytes written.
 * @param host Host header value to include.
 * @param key Client Sec-WebSocket-Key used to derive accept key.
 * @return 0 on success, non-zero on failure.
 */
int ws_make_handshake(uint8_t *out_buff, size_t *out_len, const char *host, const char *key);
#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_WSHANDSHAKE_H */
