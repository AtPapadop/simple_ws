/* websocket.h - websocket lib
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

#ifndef __SIMPLE_WS_WEBSOCKET_H
#define __SIMPLE_WS_WEBSOCKET_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "base64.h"
#include "sha1.h"

#define WS_VERSION 13
#define WS_WEBSOCK "websocket"
#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* WebSocket Frame Types */
typedef enum
{
	WS_EMPTY_FRAME = 0xF0,
	WS_ERROR_FRAME = 0xF1,
	WS_INCOMPLETE_FRAME = 0xF2,
	WS_CONTINUATION_FRAME = 0x00,
	WS_TEXT_FRAME = 0x01,
	WS_BINARY_FRAME = 0x02,
	WS_PING_FRAME = 0x09,
	WS_PONG_FRAME = 0x0A,
	WS_OPENING_FRAME = 0xF3,
	WS_CLOSING_FRAME = 0x08
} wsFrameType;

/* WebSocket Connection States */
typedef enum
{
	CONNECTING = 0, /* Connection is not yet open */
	OPEN = 1,				/* Connection is open and ready to communicate */
	CLOSING = 2,		/* Connection is in the process of closing */
	CLOSED = 3			/* Connection is closed or couldn't be opened */
} wsState;

/* WebSocket Frame Structure */
typedef struct
{
	uint8_t fin;
	uint8_t rsv1;
	uint8_t rsv2;
	uint8_t rsv3;
	uint8_t opcode;
	uint8_t *payload;
	size_t payload_length;
	wsFrameType type;
} ws_frame_t;

/* WebSocket Functions */
/**
 * @brief Parse raw WebSocket frame bytes into a frame structure.
 * @param frame Output frame structure to populate.
 * @param data Input raw frame bytes.
 * @param len Number of bytes available in data.
 * @return No return value.
 */
void ws_parse_frame(ws_frame_t *frame, uint8_t *data, size_t len);

/**
 * @brief Serialize a frame structure into WebSocket wire format bytes.
 * @param frame Input frame to serialize.
 * @param out_data Output byte buffer receiving encoded frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_frame(ws_frame_t *frame, uint8_t *out_data, size_t *out_len);

/**
 * @brief Build a standard WebSocket closing frame.
 * @param out_data Output byte buffer receiving frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_closing_frame(uint8_t *out_data, size_t *out_len);

/**
 * @brief Build a WebSocket text frame from a C string.
 * @param text Null-terminated UTF-8 text payload.
 * @param out_data Output byte buffer receiving frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_text_frame(const char *text, uint8_t *out_data, size_t *out_len);

/**
 * @brief Build a WebSocket binary frame.
 * @param data Binary payload bytes.
 * @param datalen Number of bytes in data.
 * @param out_data Output byte buffer receiving frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_binary_frame(const uint8_t *data, size_t datalen, uint8_t *out_data, size_t *out_len);

/**
 * @brief Build a control frame such as ping, pong, or close.
 * @param type Control frame opcode/type.
 * @param data Optional control payload.
 * @param data_len Number of bytes in data.
 * @param out_data Output byte buffer receiving frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_control_frame(wsFrameType type, const uint8_t *data, size_t data_len, uint8_t *out_data, size_t *out_len);

/**
 * @brief Build a WebSocket fragment frame with explicit FIN control.
 * @param type Frame type/opcode for this fragment.
 * @param fin Non-zero to mark final fragment.
 * @param data Fragment payload bytes.
 * @param data_len Number of bytes in data.
 * @param out_data Output byte buffer receiving frame bytes.
 * @param out_len Output number of bytes written to out_data.
 * @return No return value.
 */
void ws_create_fragment(wsFrameType type, bool fin, const uint8_t *data, size_t data_len, uint8_t *out_data, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_WEBSOCKET_H */
