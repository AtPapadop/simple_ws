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

#ifndef __WS_HANDSHAKE_H
#define __WS_HANDSHAKE_H

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

int ws_handshake(http_header_t *header, uint8_t *in_buf, size_t in_len, size_t *out_len);
int ws_make_accept_key(const char *key, char *out_key, size_t *out_len);
int ws_make_handshake(uint8_t *out_buff, size_t *out_len, const char *host, const char *key);
#ifdef __cplusplus
}
#endif

#endif /* __WS_HANDSHAKE_H */
