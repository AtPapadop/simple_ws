/* wsserver.h - websocket lib
 *
 * Copyright (C) 2026 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 */
#ifndef __WSSERVER_H
#define __WSSERVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "websocket.h"
#include "wshandshake.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct ws_server ws_server_t;
typedef struct ws_client ws_client_t;

/*
 * Callback notes:
 * - message payload points into an internal buffer and is only valid during the callback execution.
 * - if you want to keep the payload, copy it to your own buffer.
*/

typedef void (*ws_server_connect_handler_t)(ws_server_t *server, ws_client_t *client, void *user_data);
typedef void (*ws_server_message_handler_t)(ws_server_t *server, ws_client_t *client, const ws_frame_t *frame, void *user_data);
typedef void (*ws_server_disconnect_handler_t)(ws_server_t *server, ws_client_t *client, const char *reason, void *user_data);
typedef void (*ws_server_handshake_fail_handler_t)(ws_server_t *server, ws_client_t *client, const char *reason, void *user_data);
typedef void (*ws_server_error_handler_t)(ws_server_t *server, const char *where, int error_code, void *user_data);

/* Lifecycle functions */
ws_server_t *ws_server_create(uint16_t port, int max_clients);
void ws_server_destroy(ws_server_t *server);

/* Optional configuration functions */
int ws_server_set_bind_address(ws_server_t *server, const char *ip);
int ws_server_set_backlog(ws_server_t *server, int backlog);
int ws_server_set_initial_buffer_size(ws_server_t *server, size_t bytes);
int ws_server_set_max_events(ws_server_t *server, int max_events);

/* Callback functions */
void ws_server_set_connect_handler(ws_server_t *server, ws_server_connect_handler_t handler);
void ws_server_set_message_handler(ws_server_t *server, ws_server_message_handler_t handler);
void ws_server_set_disconnect_handler(ws_server_t *server, ws_server_disconnect_handler_t handler);
void ws_server_set_handshake_fail_handler(ws_server_t *server, ws_server_handshake_fail_handler_t handler);
void ws_server_set_error_handler(ws_server_t *server, ws_server_error_handler_t handler);

/* Opaque User Pointers */
void ws_server_set_user_data(ws_server_t *server, void *user_data);
void *ws_server_get_user_data(ws_server_t *server);

void ws_client_set_user_data(ws_client_t *client, void *user_data);
void *ws_client_get_user_data(ws_client_t *client);

/* Run Control */
int ws_server_run(ws_server_t *server); /* Blocking call, current thread */
int ws_server_start(ws_server_t *server); /* spawns internal thread */
int ws_server_stop(ws_server_t *server);
int ws_server_join(ws_server_t *server); /* Waits for server thread to finish */

/* Sending */
int ws_server_send_text(ws_server_t *server, ws_client_t *client, const char *text);
int ws_server_send_binary(ws_server_t *server, ws_client_t *client, const uint8_t *data, size_t len);
int ws_server_send_ping(ws_server_t *server, ws_client_t *client);
int ws_server_send_pong(ws_server_t *server, ws_client_t *client);
int ws_server_send_frame(ws_server_t *server, ws_client_t *client, wsFrameType type, const uint8_t *payload, size_t payload_len);

int ws_server_broadcast_text(ws_server_t *server, const char *text);
int ws_server_broadcast_binary(ws_server_t *server, const uint8_t *data, size_t len);
int ws_server_broadcast_ping(ws_server_t *server);
int ws_server_broadcast_frame(ws_server_t *server, wsFrameType type, const uint8_t *payload, size_t payload_len);

/* Request Graceful Shutdown */
int ws_server_close_client(ws_server_t *server, ws_client_t *client, const char *reason);

/* Helpers */
int ws_client_fd(const ws_client_t *client);
bool ws_client_is_connected(const ws_client_t *client);
bool ws_client_handshake_done(const ws_client_t *client);
const char *ws_client_ip(const ws_client_t *client, char *buffer, size_t len);
uint16_t ws_client_port(const ws_client_t *client);
size_t ws_server_client_count(ws_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* __WSSERVER_H_ */