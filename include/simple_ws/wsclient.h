/* wsclient.h - websocket client API
 *
 * Copyright (C) 2026 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 */

#ifndef __SIMPLE_WS_WSCLIENT_H
#define __SIMPLE_WS_WSCLIENT_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "websocket.h"

typedef struct ws_remote_client ws_remote_client_t;

/**
 * @brief Create a remote WebSocket client handle.
 * @return Allocated client handle, or NULL on failure.
 */
ws_remote_client_t *ws_remote_client_create(void);

/**
 * @brief Destroy a client handle and close the underlying socket.
 * @param client Client handle to destroy.
 * @return No return value.
 */
void ws_remote_client_destroy(ws_remote_client_t *client);

/**
 * @brief Connect and complete HTTP upgrade handshake with a remote server.
 * @param client Client handle.
 * @param host Remote hostname or IP.
 * @param port Remote TCP port.
 * @param path Request path, for example "/".
 * @param origin Optional Origin header value, or NULL.
 * @param timeout_ms Connect/handshake timeout in milliseconds. Use <= 0 for blocking.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_connect(ws_remote_client_t *client, const char *host, uint16_t port, const char *path, const char *origin, int timeout_ms);

/**
 * @brief Close the TCP connection immediately.
 * @param client Client handle.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_disconnect(ws_remote_client_t *client);

/**
 * @brief Send a frame with explicit opcode and FIN bit.
 * @param client Client handle.
 * @param type Frame opcode/type.
 * @param fin Non-zero when this is the final fragment.
 * @param payload Payload bytes, may be NULL when payload_len is 0.
 * @param payload_len Payload length in bytes.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_frame(ws_remote_client_t *client, wsFrameType type, bool fin, const uint8_t *payload, size_t payload_len);

/**
 * @brief Send a UTF-8 text frame.
 * @param client Client handle.
 * @param text Null-terminated UTF-8 string.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_text(ws_remote_client_t *client, const char *text);

/**
 * @brief Send a binary frame.
 * @param client Client handle.
 * @param data Binary payload bytes.
 * @param len Payload length in bytes.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_binary(ws_remote_client_t *client, const uint8_t *data, size_t len);

/**
 * @brief Send a ping frame.
 * @param client Client handle.
 * @param payload Optional ping payload.
 * @param payload_len Payload length in bytes, must be <= 125.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_ping(ws_remote_client_t *client, const uint8_t *payload, size_t payload_len);

/**
 * @brief Send a pong frame.
 * @param client Client handle.
 * @param payload Optional pong payload.
 * @param payload_len Payload length in bytes, must be <= 125.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_pong(ws_remote_client_t *client, const uint8_t *payload, size_t payload_len);

/**
 * @brief Send a close frame.
 * @param client Client handle.
 * @param code Close code in host byte order.
 * @param reason Optional UTF-8 reason text.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_send_close(ws_remote_client_t *client, uint16_t code, const char *reason);

/**
 * @brief Receive one full WebSocket frame from the remote server.
 * @param client Client handle.
 * @param frame Parsed frame view.
 * @param frame_buf Receives malloc'ed frame bytes backing frame->payload.
 * @param frame_buf_len Receives number of bytes in frame_buf.
 * @param timeout_ms Read timeout in milliseconds. Use <= 0 for blocking.
 * @return 0 on success, non-zero on failure.
 */
int ws_remote_client_receive_frame(ws_remote_client_t *client, ws_frame_t *frame, uint8_t **frame_buf, size_t *frame_buf_len, int timeout_ms);

/**
 * @brief Free a frame buffer returned by ws_remote_client_receive_frame.
 * @param frame_buf Buffer pointer to free.
 * @return No return value.
 */
void ws_remote_client_free_frame_buffer(uint8_t *frame_buf);

/**
 * @brief Query whether the client is connected.
 * @param client Client handle.
 * @return true when connected, otherwise false.
 */
bool ws_remote_client_is_connected(const ws_remote_client_t *client);

/**
 * @brief Get the underlying socket descriptor.
 * @param client Client handle.
 * @return Socket fd on success, or -1 on failure.
 */
int ws_remote_client_fd(const ws_remote_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_WSCLIENT_H */
