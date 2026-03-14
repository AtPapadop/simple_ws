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
#ifndef __SIMPLE_WS_WSSERVER_H
#define __SIMPLE_WS_WSSERVER_H

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

/**
 * @brief Create a WebSocket server instance.
 * @param port TCP port to listen on.
 * @param max_clients Maximum simultaneous client connections.
 * @return Pointer to created server instance, or NULL on failure.
 */
ws_server_t *ws_server_create(uint16_t port, int max_clients);

/**
 * @brief Destroy a server instance and release all associated resources.
 * @param server Server instance to destroy.
 * @return No return value.
 */
void ws_server_destroy(ws_server_t *server);

/* Optional configuration functions */
/**
 * @brief Set the local bind address used by the server socket.
 * @param server Server instance to configure.
 * @param ip IPv4/IPv6 textual address to bind to.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_set_bind_address(ws_server_t *server, const char *ip);

/**
 * @brief Set the listen backlog for pending connection queue.
 * @param server Server instance to configure.
 * @param backlog Backlog size passed to listen().
 * @return 0 on success, non-zero on failure.
 */
int ws_server_set_backlog(ws_server_t *server, int backlog);

/**
 * @brief Set initial per-client receive buffer size.
 * @param server Server instance to configure.
 * @param bytes Buffer size in bytes.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_set_initial_buffer_size(ws_server_t *server, size_t bytes);

/**
 * @brief Set maximum number of poll/epoll events processed per iteration.
 * @param server Server instance to configure.
 * @param max_events Maximum events per loop iteration.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_set_max_events(ws_server_t *server, int max_events);

/* Callback functions */
/**
 * @brief Register callback invoked when a client connects.
 * @param server Server instance to configure.
 * @param handler Callback function pointer, or NULL to clear.
 * @return No return value.
 */
void ws_server_set_connect_handler(ws_server_t *server, ws_server_connect_handler_t handler);

/**
 * @brief Register callback invoked when a data frame is received.
 * @param server Server instance to configure.
 * @param handler Callback function pointer, or NULL to clear.
 * @return No return value.
 */
void ws_server_set_message_handler(ws_server_t *server, ws_server_message_handler_t handler);

/**
 * @brief Register callback invoked when a client disconnects.
 * @param server Server instance to configure.
 * @param handler Callback function pointer, or NULL to clear.
 * @return No return value.
 */
void ws_server_set_disconnect_handler(ws_server_t *server, ws_server_disconnect_handler_t handler);

/**
 * @brief Register callback invoked when handshake fails.
 * @param server Server instance to configure.
 * @param handler Callback function pointer, or NULL to clear.
 * @return No return value.
 */
void ws_server_set_handshake_fail_handler(ws_server_t *server, ws_server_handshake_fail_handler_t handler);

/**
 * @brief Register callback invoked for internal server errors.
 * @param server Server instance to configure.
 * @param handler Callback function pointer, or NULL to clear.
 * @return No return value.
 */
void ws_server_set_error_handler(ws_server_t *server, ws_server_error_handler_t handler);

/* Opaque User Pointers */
/**
 * @brief Associate application-owned user data with a server instance.
 * @param server Server instance to update.
 * @param user_data Opaque pointer stored by the server.
 * @return No return value.
 */
void ws_server_set_user_data(ws_server_t *server, void *user_data);

/**
 * @brief Get the user data pointer previously set on a server instance.
 * @param server Server instance to query.
 * @return Stored user data pointer, or NULL when not set.
 */
void *ws_server_get_user_data(ws_server_t *server);

/**
 * @brief Associate application-owned user data with a client.
 * @param client Client instance to update.
 * @param user_data Opaque pointer stored by the client.
 * @return No return value.
 */
void ws_client_set_user_data(ws_client_t *client, void *user_data);

/**
 * @brief Get the user data pointer previously set on a client.
 * @param client Client instance to query.
 * @return Stored user data pointer, or NULL when not set.
 */
void *ws_client_get_user_data(ws_client_t *client);

/* Run Control */
/**
 * @brief Run the server loop in the current thread.
 * @param server Server instance to run.
 * @return 0 on normal shutdown, non-zero on failure.
 */
int ws_server_run(ws_server_t *server); /* Blocking call, current thread */

/**
 * @brief Start the server loop in an internal background thread.
 * @param server Server instance to start.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_start(ws_server_t *server);

/**
 * @brief Request server shutdown.
 * @param server Server instance to stop.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_stop(ws_server_t *server);

/**
 * @brief Wait for the internal server thread to terminate.
 * @param server Server instance to join.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_join(ws_server_t *server); /* Waits for server thread to finish */

/* Sending */
/**
 * @brief Send a text frame to a specific client.
 * @param server Server instance.
 * @param client Target client.
 * @param text Null-terminated UTF-8 text payload.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_send_text(ws_server_t *server, ws_client_t *client, const char *text);

/**
 * @brief Send a binary frame to a specific client.
 * @param server Server instance.
 * @param client Target client.
 * @param data Binary payload bytes.
 * @param len Number of bytes in data.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_send_binary(ws_server_t *server, ws_client_t *client, const uint8_t *data, size_t len);

/**
 * @brief Send a ping control frame to a specific client.
 * @param server Server instance.
 * @param client Target client.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_send_ping(ws_server_t *server, ws_client_t *client);

/**
 * @brief Send a pong control frame to a specific client.
 * @param server Server instance.
 * @param client Target client.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_send_pong(ws_server_t *server, ws_client_t *client);

/**
 * @brief Send a custom frame to a specific client.
 * @param server Server instance.
 * @param client Target client.
 * @param type Frame type/opcode to send.
 * @param payload Frame payload bytes.
 * @param payload_len Number of bytes in payload.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_send_frame(ws_server_t *server, ws_client_t *client, wsFrameType type, const uint8_t *payload, size_t payload_len);

/**
 * @brief Broadcast a text frame to all connected clients.
 * @param server Server instance.
 * @param text Null-terminated UTF-8 text payload.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_broadcast_text(ws_server_t *server, const char *text);

/**
 * @brief Broadcast a binary frame to all connected clients.
 * @param server Server instance.
 * @param data Binary payload bytes.
 * @param len Number of bytes in data.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_broadcast_binary(ws_server_t *server, const uint8_t *data, size_t len);

/**
 * @brief Broadcast a ping frame to all connected clients.
 * @param server Server instance.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_broadcast_ping(ws_server_t *server);

/**
 * @brief Broadcast a custom frame to all connected clients.
 * @param server Server instance.
 * @param type Frame type/opcode to send.
 * @param payload Frame payload bytes.
 * @param payload_len Number of bytes in payload.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_broadcast_frame(ws_server_t *server, wsFrameType type, const uint8_t *payload, size_t payload_len);

/* Request Graceful Shutdown */
/**
 * @brief Request graceful closure of a connected client.
 * @param server Server instance.
 * @param client Target client.
 * @param reason Optional close reason text.
 * @return 0 on success, non-zero on failure.
 */
int ws_server_close_client(ws_server_t *server, ws_client_t *client, const char *reason);

/* Helpers */
/**
 * @brief Get the native socket/file descriptor for a client.
 * @param client Client instance to query.
 * @return Native descriptor value, or negative on error.
 */
int ws_client_fd(const ws_client_t *client);

/**
 * @brief Check whether a client is currently connected.
 * @param client Client instance to query.
 * @return true if connected, otherwise false.
 */
bool ws_client_is_connected(const ws_client_t *client);

/**
 * @brief Check whether the WebSocket handshake has completed.
 * @param client Client instance to query.
 * @return true if handshake is complete, otherwise false.
 */
bool ws_client_handshake_done(const ws_client_t *client);

/**
 * @brief Get client IP address as a string.
 * @param client Client instance to query.
 * @param buffer Output character buffer for textual IP address.
 * @param len Capacity of buffer in bytes.
 * @return Pointer to buffer on success, or NULL on failure.
 */
const char *ws_client_ip(const ws_client_t *client, char *buffer, size_t len);

/**
 * @brief Get remote client port number.
 * @param client Client instance to query.
 * @return Client TCP port in host byte order.
 */
uint16_t ws_client_port(const ws_client_t *client);

/**
 * @brief Count currently connected clients.
 * @param server Server instance to query.
 * @return Number of connected clients.
 */
size_t ws_server_client_count(ws_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_WSSERVER_H_ */