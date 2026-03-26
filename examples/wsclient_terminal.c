/*
 * Terminal WebSocket client example for simple_ws
 *
 * Copyright (C) 2026 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simple_ws/simple_ws.h>

static void print_usage(const char *prog)
{
  fprintf(stderr, "Usage: %s <host> <port> [path]\n", prog ? prog : "wsclient_terminal");
  fprintf(stderr, "Example: %s 127.0.0.1 8888 /\n", prog ? prog : "wsclient_terminal");
}

static void trim_newline(char *s)
{
  if (!s)
    return;

  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
  {
    s[len - 1] = '\0';
    len--;
  }
}

static void print_received_frame(const ws_frame_t *frame)
{
  if (!frame)
    return;

  switch (frame->type)
  {
  case WS_TEXT_FRAME:
  {
    char *text = malloc(frame->payload_length + 1);
    if (!text)
    {
      fprintf(stderr, "OOM while printing text frame\n");
      return;
    }
    memcpy(text, frame->payload, frame->payload_length);
    text[frame->payload_length] = '\0';
    printf("[recv text] %s\n", text);
    free(text);
    break;
  }
  case WS_BINARY_FRAME:
    printf("[recv binary] %zu bytes\n", frame->payload_length);
    break;
  case WS_CONTINUATION_FRAME:
    printf("[recv continuation] fin=%u len=%zu\n", frame->fin, frame->payload_length);
    break;
  case WS_PING_FRAME:
    printf("[recv ping] len=%zu\n", frame->payload_length);
    break;
  case WS_PONG_FRAME:
    printf("[recv pong] len=%zu\n", frame->payload_length);
    break;
  case WS_CLOSING_FRAME:
    printf("[recv close] len=%zu\n", frame->payload_length);
    break;
  default:
    printf("[recv frame] type=%d fin=%u len=%zu\n", frame->type, frame->fin, frame->payload_length);
    break;
  }
}

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char *host = argv[1];
  int port_i = atoi(argv[2]);
  const char *path = (argc > 3) ? argv[3] : "/";

  if (port_i <= 0 || port_i > 65535)
  {
    fprintf(stderr, "Invalid port: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

  ws_remote_client_t *client = ws_remote_client_create();
  if (!client)
  {
    perror("ws_remote_client_create");
    return EXIT_FAILURE;
  }

  if (ws_remote_client_connect(client, host, (uint16_t)port_i, path, NULL, 5000) != 0)
  {
    perror("ws_remote_client_connect");
    ws_remote_client_destroy(client);
    return EXIT_FAILURE;
  }

  printf("Connected to ws://%s:%d%s\n", host, port_i, path);
  printf("Type text and press Enter to send. Commands: /quit, /ping, /close\n");

  int running = 1;
  int close_sent = 0;
  while (running && ws_remote_client_is_connected(client))
  {
    struct pollfd pfds[2];
    pfds[0].fd = 0;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    pfds[1].fd = ws_remote_client_fd(client);
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;

    int rc = poll(pfds, 2, close_sent ? 2000 : -1);
    if (rc < 0)
    {
      perror("poll");
      break;
    }

    if (rc == 0)
    {
      if (close_sent)
      {
        printf("close handshake timeout, exiting...\n");
        break;
      }
      continue;
    }

    if ((pfds[0].revents & POLLIN) != 0)
    {
      char line[2048];
      if (!fgets(line, sizeof(line), stdin))
      {
        printf("stdin closed, quitting...\n");
        break;
      }

      trim_newline(line);
      if (line[0] == '\0')
        continue;

      if (strcmp(line, "/quit") == 0)
      {
        if (ws_remote_client_send_close(client, 1000, "Client quit") != 0)
          perror("ws_remote_client_send_close");
        close_sent = 1;
        continue;
      }

      if (strcmp(line, "/ping") == 0)
      {
        if (ws_remote_client_send_ping(client, NULL, 0) != 0)
          perror("ws_remote_client_send_ping");
        continue;
      }

      if (strcmp(line, "/close") == 0)
      {
        if (ws_remote_client_send_close(client, 1000, "Client close") != 0)
          perror("ws_remote_client_send_close");
        close_sent = 1;
        continue;
      }

      if (ws_remote_client_send_text(client, line) != 0)
      {
        perror("ws_remote_client_send_text");
        break;
      }
    }

    if ((pfds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      ws_frame_t frame;
      uint8_t *raw = NULL;
      size_t raw_len = 0;

      if (ws_remote_client_receive_frame(client, &frame, &raw, &raw_len, 1) != 0)
      {
        if (errno == ETIMEDOUT)
          continue;

        perror("ws_remote_client_receive_frame");
        running = 0;
        continue;
      }

      print_received_frame(&frame);

      if (frame.type == WS_PING_FRAME)
      {
        if (ws_remote_client_send_pong(client, frame.payload, frame.payload_length) != 0)
          perror("ws_remote_client_send_pong");
      }
      else if (frame.type == WS_CLOSING_FRAME)
      {
        if (!close_sent)
          (void)ws_remote_client_send_close(client, 1000, "Client ack close");
        running = 0;
      }

      ws_remote_client_free_frame_buffer(raw);
    }
  }

  (void)ws_remote_client_disconnect(client);
  ws_remote_client_destroy(client);
  printf("Disconnected.\n");
  return EXIT_SUCCESS;
}
