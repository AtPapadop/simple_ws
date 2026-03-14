/* websocket.c - websocket lib
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
#include <string.h>
#include <assert.h>

#include "websocket.h"

#define MASK_LEN 4

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HTONS(v) ((v >> 8) | (v << 8))
#else
#define HTONS(v) (v)
#endif

/* Internal Functions */

/* Function to create a WebSocket frame */
static void ws_create_frame_impl(wsFrameType type, bool fin, const uint8_t *payload, size_t payload_len, uint8_t *out_data, size_t *out_len)
{
	bool mask = false;
	size_t header = 2;

	out_data[0] = (fin ? 0x80 : 0x00) | (uint8_t)type;
	out_data[1] = (payload_len <= 0x7D ? payload_len : (payload_len <= 0xFFFF ? 0x7E : 0x7F)) | (mask ? 0x80 : 0);

	if (payload_len > 0x7D)
	{
		if (payload_len <= 0xFFFF)
		{
			out_data[2] = (payload_len >> 8) & 0xFF;
			out_data[3] = payload_len & 0xFF;
			header += 2;
		}
		else
		{
			for (int i = 0; i < 8; i++)
				out_data[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
			header += 8;
		}
	}

	if (mask)
	{
		uint8_t mask_key[MASK_LEN];
		for (int i = 0; i < MASK_LEN; i++)
			mask_key[i] = rand() & 0xFF;
		memcpy(&out_data[header], mask_key, MASK_LEN);
		header += MASK_LEN;

		for (size_t i = 0; i < payload_len; i++)
			out_data[header + i] = payload[i] ^ mask_key[i % MASK_LEN];
	}
	else if (payload && payload_len > 0)
	{
		memcpy(&out_data[header], payload, payload_len);
	}

	*out_len = header + payload_len;
}

/* Parses the opcode and assigns frame type */
static wsFrameType ws_parse_opcode(ws_frame_t *frame)
{
	switch (frame->opcode)
	{
	case WS_CONTINUATION_FRAME:
	case WS_TEXT_FRAME:
	case WS_BINARY_FRAME:
	case WS_CLOSING_FRAME:
	case WS_PING_FRAME:
	case WS_PONG_FRAME:
		frame->type = frame->opcode;
		break;
	default:
		frame->type = WS_ERROR_FRAME; /* Reserved frames treated as errors */
		break;
	}
	return frame->type;
}

/* Checks if the opcode is a control frame */
static bool ws_is_control_opcode(uint8_t opcode)
{
	return opcode == WS_CLOSING_FRAME || opcode == WS_PING_FRAME || opcode == WS_PONG_FRAME;
}

void ws_create_frame(ws_frame_t *frame, uint8_t *out_data, size_t *out_len)
{
	assert(frame);
	assert(frame->type == WS_TEXT_FRAME || frame->type == WS_BINARY_FRAME || frame->type == WS_PING_FRAME || frame->type == WS_PONG_FRAME || frame->type == WS_CLOSING_FRAME);
	ws_create_frame_impl(frame->type, frame->fin, frame->payload, frame->payload_length, out_data, out_len);
}

void ws_parse_frame(ws_frame_t *frame, uint8_t *data, size_t len)
{
	if (len < 2)
	{
		frame->type = WS_INCOMPLETE_FRAME;
		return;
	}

	frame->fin = (data[0] & 0x80) != 0;

	if ((data[0] & 0x70) != 0)
	{
		frame->type = WS_ERROR_FRAME;
		return;
	}

	frame->opcode = data[0] & 0x0F;

	if (ws_parse_opcode(frame) == WS_ERROR_FRAME)
		return;

	bool masked = (data[1] & 0x80) != 0;
	uint64_t payloadLength = (uint64_t)(data[1] & 0x7F);
	size_t headerSize = 2;

	if (payloadLength == 0x7E)
	{
		if (len < 4)
		{
			frame->type = WS_INCOMPLETE_FRAME;
			return;
		}
		payloadLength = ((uint64_t)data[2] << 8) | ((uint64_t)data[3]);
		headerSize += 2;
	}
	else if (payloadLength == 0x7F)
	{
		if (len < 10)
		{
			frame->type = WS_INCOMPLETE_FRAME;
			return;
		}
		payloadLength = 0;
		for (int i = 0; i < 8; i++)
			payloadLength = (payloadLength << 8) | (uint64_t)data[2 + i];
		headerSize += 8;
	}

	if (ws_is_control_opcode(frame->opcode))
	{
		if (!frame->fin || payloadLength > 125)
		{
			frame->type = WS_ERROR_FRAME;
			return;
		}
	}

	if (masked)
	{
		if (len < headerSize + MASK_LEN)
		{
			frame->type = WS_INCOMPLETE_FRAME;
			return;
		}
		headerSize += MASK_LEN;
	}

	if (payloadLength > (uint64_t)(SIZE_MAX - headerSize))
	{
		frame->type = WS_ERROR_FRAME;
		return;
	}

	if ((size_t)payloadLength > len - headerSize)
	{
		frame->type = WS_INCOMPLETE_FRAME;
		return;
	}

	frame->payload = &data[headerSize];
	frame->payload_length = (size_t)payloadLength;

	if (masked)
	{
		uint8_t maskingKey[MASK_LEN];
		memcpy(maskingKey, &data[headerSize - MASK_LEN], MASK_LEN);

		for (size_t i = 0; i < frame->payload_length; i++)
		{
			frame->payload[i] ^= maskingKey[i % MASK_LEN];
		}
	}
}

/* Functions to create different types of frames */

void ws_create_ping_frame(uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = 0, .payload = NULL, .type = WS_PING_FRAME};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_pong_frame(uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = 0, .payload = NULL, .type = WS_PONG_FRAME};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_closing_frame(uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = 0, .payload = NULL, .type = WS_CLOSING_FRAME};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_text_frame(const char *text, uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = strlen(text), .payload = (uint8_t *)text, .type = WS_TEXT_FRAME};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_binary_frame(const uint8_t *data, size_t datalen, uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = datalen, .payload = (uint8_t *)data, .type = WS_BINARY_FRAME};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_control_frame(wsFrameType type, const uint8_t *data, size_t data_len, uint8_t *out_data, size_t *out_len)
{
	ws_frame_t frame = {.payload_length = data_len, .payload = (uint8_t *)data, .type = type};
	ws_create_frame(&frame, out_data, out_len);
}

void ws_create_fragment(wsFrameType type, bool fin, const uint8_t *data, size_t data_len, uint8_t *out_data, size_t *out_len)
{
	assert(type == WS_CONTINUATION_FRAME || type == WS_TEXT_FRAME || type == WS_BINARY_FRAME || type == WS_PING_FRAME || type == WS_PONG_FRAME || type == WS_CLOSING_FRAME);
	ws_create_frame_impl(type, fin, data, data_len, out_data, out_len);
}
