/*
 * Echo server example for simple_ws
 *
 * Copyright (C) 2026 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

#include <simple_ws/simple_ws.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig)
{
  (void)sig;
  g_stop = 1;
}

static void on_connect(ws_server_t *server, ws_client_t *client, void *user_data)
{
  (void)user_data;

  char ip[65];
  const char *addr = ws_client_ip(client, ip, sizeof(ip));
  if (!addr)
    addr = "Unknown";
  printf("Client connected: %s:%u\n", addr, ws_client_port(client));
  ws_server_send_text(server, client, "Welcome to the Echo Server!");
}

static void on_disconnect(ws_server_t *server, ws_client_t *client, const char *reason, void *user_data)
{
  (void)server;
  (void)user_data;

  char ip[65];
  const char *addr = ws_client_ip(client, ip, sizeof(ip));
  if (!addr)
    addr = "Unknown";
  printf("Client disconnected: %s:%u, Reason: %s\n", addr, ws_client_port(client), reason ? reason : "No reason");
}

static void on_error(ws_server_t *server, const char *where, int error_code, void *user_data)
{
  (void)server;
  (void)user_data;

  fprintf(stderr, "Error at %s: %s\n", where ? where : "Unknown", strerror(error_code));
}

static void on_handshake_fail(ws_server_t *server, ws_client_t *client, const char *reason, void *user_data)
{
  (void)server;
  (void)user_data;

  char ip[65];
  const char *addr = ws_client_ip(client, ip, sizeof(ip));
  if (!addr)
    addr = "Unknown";
  fprintf(stderr, "Handshake failed for client %s:%u, Reason: %s\n", addr, ws_client_port(client), reason ? reason : "No reason");
}

static void on_message(ws_server_t *server, ws_client_t *client, const ws_frame_t *frame, void *user_data)
{
  (void)user_data;

  switch (frame->type)
  {
  case WS_TEXT_FRAME:
    char *text = malloc(frame->payload_length + 1);
    if (!text)
    {
      fprintf(stderr, "Failed to allocate memory for received message\n");
      ws_server_send_text(server, client, "Server error: Unable to process message");
      return;
    }
    memcpy(text, frame->payload, frame->payload_length);
    text[frame->payload_length] = '\0';

    printf("TEXT from fd %d: %s\n", ws_client_fd(client), text);

    if (strncmp(text, "quit", 4) == 0)
    {
      ws_server_send_text(server, client, "Goodbye!");
      ws_server_close_client(server, client, "Client requested disconnect");
      free(text);
      return;
    }
    ws_server_send_text(server, client, text);
    free(text);
    break;
  case WS_BINARY_FRAME:
    printf("BINARY from fd %d: %zu bytes\n", ws_client_fd(client), frame->payload_length);
    ws_server_send_binary(server, client, frame->payload, frame->payload_length);
    break;
  case WS_PING_FRAME:
    printf("PING from fd %d\n", ws_client_fd(client));
    ws_server_send_pong(server, client);
    break;
  case WS_PONG_FRAME:
    printf("PONG from fd %d\n", ws_client_fd(client));
    break;
  default:
    printf("Received unsupported frame type %d from fd %d\n", frame->type, ws_client_fd(client));
    break;
  }
}

int main(int argc, char *argv[])
{
  int port = 8888;
  if (argc > 1)
    port = atoi(argv[1]);

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  ws_server_t *server = ws_server_create(port, 128);
  if (!server)
  {
    fprintf(stderr, "Failed to create WebSocket server\n");
    return EXIT_FAILURE;
  }

  ws_server_set_connect_handler(server, on_connect);
  ws_server_set_disconnect_handler(server, on_disconnect);
  ws_server_set_message_handler(server, on_message);
  ws_server_set_error_handler(server, on_error);
  ws_server_set_handshake_fail_handler(server, on_handshake_fail);

  if (ws_server_start(server) != 0)
  {
    fprintf(stderr, "Failed to start WebSocket server\n");
    ws_server_destroy(server);
    return EXIT_FAILURE;
  }

  printf("Echo server listening on ws://0.0.0.0:%d\n", port);
  printf("Press Ctrl+C to stop the server...\n");

  while (!g_stop)
  {
    sleep(1);
  }

  printf("Shutting down server...\n");
  ws_server_stop(server);
  ws_server_join(server);
  ws_server_destroy(server);
  printf("Server stopped. Goodbye!\n");
  return EXIT_SUCCESS;
}