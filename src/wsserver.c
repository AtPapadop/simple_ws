/* wsserver.c - websocket lib
 *
 * Copyright (C) 2026 Athanasios Papadopoulos
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 */
#define _POSIX_C_SOURCE 200809L

#include "wsserver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define WS_SERVER_DEFAULT_BIND_ADDR "0.0.0.0"
#define WS_SERVER_DEFAULT_BACKLOG 128
#define WS_SERVER_DEFAULT_BUF_SIZE 4096
#define WS_SERVER_DEFAULT_MAX_EVENTS 64

#define WS_TAG_FD(fd_) ((((uint64_t)(uint32_t)(fd_)) << 1) | 0ULL)
#define WS_TAG_PTR(ptr_) ((((uint64_t)(uintptr_t)(ptr_)) << 1) | 1ULL)
#define WS_TAG_IS_PTR(tag_) (((tag_) & 1ULL) != 0ULL)
#define WS_TAG_TO_FD(tag_) ((int)((tag_) >> 1))
#define WS_TAG_TO_PTR(tag_) ((void *)(uintptr_t)((tag_) >> 1))

struct ws_client
{
  int fd;
  bool active;
  bool handshake_done;
  bool close_requested;

  struct sockaddr_storage addr;
  socklen_t addr_len;

  uint8_t *in_buf;
  size_t in_len;
  size_t in_cap;

  uint8_t *out_buf;
  size_t out_len;
  size_t out_sent;
  size_t out_cap;

  bool frag_active;
  wsFrameType frag_type;
  uint8_t *frag_buf;
  size_t frag_len;
  size_t frag_cap;

  uint16_t fails;

  void *user_data;
  char close_reason[128];
};

struct ws_server
{
  int port;
  int max_clients;
  int backlog;
  int max_events;

  char bind_addr[INET6_ADDRSTRLEN];
  size_t initial_buffer_size;

  int listen_fd;
  int epoll_fd;
  int wake_fd;

  bool started_as_thread;
  bool thead_joinable;
  bool running;

  pthread_t thead;
  pthread_mutex_t lock;

  ws_client_t *clients;
  void *user_data;

  ws_server_connect_handler_t on_connect;
  ws_server_message_handler_t on_message;
  ws_server_disconnect_handler_t on_disconnect;
  ws_server_handshake_fail_handler_t on_handshake_fail;
  ws_server_error_handler_t on_error;
};

/* Internal Helper Functions */

static void ws_server_report_error(ws_server_t *server, const char *where, int error_code)
{
  if (server && server->on_error)
    server->on_error(server, where, error_code, server->user_data);
}

static int ws_set_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    flags = 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void ws_client_zero(ws_client_t *client)
{
  if (!client)
    return;

  client->fd = -1;
  client->active = false;
  client->handshake_done = false;
  client->close_requested = false;

  memset(&client->addr, 0, sizeof(client->addr));
  client->addr_len = 0;

  client->in_buf = NULL;
  client->in_len = 0;
  client->in_cap = 0;

  client->out_buf = NULL;
  client->out_len = 0;
  client->out_sent = 0;
  client->out_cap = 0;

  client->frag_active = false;
  client->frag_type = WS_EMPTY_FRAME;
  client->frag_buf = NULL;
  client->frag_len = 0;
  client->frag_cap = 0;

  client->fails = 0;
  client->user_data = NULL;
  client->close_reason[0] = '\0';
}

static void ws_client_reset_runtime(ws_client_t *client)
{
  if (!client)
    return;

  client->fd = -1;
  client->active = false;
  client->handshake_done = false;
  client->close_requested = false;

  memset(&client->addr, 0, sizeof(client->addr));
  client->addr_len = 0;

  client->in_len = 0;
  client->out_len = 0;
  client->out_sent = 0;
  client->fails = 0;
  client->frag_active = false;
  client->frag_type = WS_EMPTY_FRAME;
  client->frag_len = 0;
  client->user_data = NULL;
  client->close_reason[0] = '\0';
}

static void ws_client_free_buffers(ws_client_t *client)
{
  if (!client)
    return;

  free(client->in_buf);
  client->in_buf = NULL;
  client->in_len = 0;
  client->in_cap = 0;

  free(client->out_buf);
  client->out_buf = NULL;
  client->out_len = 0;
  client->out_sent = 0;
  client->out_cap = 0;

  free(client->frag_buf);
  client->frag_buf = NULL;
  client->frag_len = 0;
  client->frag_cap = 0;
}

static int ws_ensure_capacity(uint8_t **buf, size_t *cap, size_t needed, size_t initial)
{
  if (!buf || !cap)
    return -1;
  if (*cap >= needed)
    return 0;

  size_t new_cap = (*cap == 0) ? initial : *cap;
  if (new_cap == 0)
    new_cap = WS_SERVER_DEFAULT_BUF_SIZE;

  while (new_cap < needed)
    if (new_cap > SIZE_MAX / 2)
      return -1;
  new_cap *= 2;

  uint8_t *tmp = realloc(*buf, new_cap);
  if (!tmp)
    return -1;
  *buf = tmp;
  *cap = new_cap;
  return 0;
}

static bool ws_server_is_running(ws_server_t *server)
{
  bool running = false;
  if (!server)
    return false;

  pthread_mutex_lock(&server->lock);
  running = server->running;
  pthread_mutex_unlock(&server->lock);
  return running;
}

static void ws_server_set_running(ws_server_t *server, bool running)
{
  if (!server || server->wake_fd < 0)
    return;

  (void)write(server->wake_fd, &(uint64_t){1}, sizeof(uint64_t)); // Wake up epoll_wait
}

static int ws_server_mod_client_events(ws_server_t *server, ws_client_t *client, uint32_t events)
{
  if (!server || !client || client->fd < 0 || server->epoll_fd < 0)
    return -1;

  struct epoll_event ev = {0};
  ev.events = events;
  ev.data.u64 = WS_TAG_PTR(client);

  return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
}

static size_t ws_find_http_header_t_end(uint8_t *buf, size_t len)
{
  if (!buf || len < 2)
    return 0;

  for (size_t i = 0; i + 3 < len; i++)
  {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
      return i + 4;
  }

  for (size_t i = 0; i + 1 < len; i++)
  {
    if (buf[i] == '\n' && buf[i + 1] == '\n')
      return i + 2;
  }
  return 0;
}

static int ws_peak_frame_wire_size(const uint8_t *data, size_t len, size_t *frame_size)
{
  if (!data || !frame_size)
    return -1;
  if (len < 2)
    return 0;

  size_t header_size = 2;
  uint64_t payload_len = (uint64_t)(data[1] & 0x7F);

  if (payload_len == 0x7E)
  {
    if (len < 4)
      return 0;
    payload_len = ((uint64_t)data[2] << 8) | ((uint64_t)data[3]);
    header_size += 2;
  }
  else if (payload_len == 0x7F)
  {
    if (len < 10)
      return 0;
    payload_len = 0;
    for (int i = 0; i < 8; i++)
      payload_len = (payload_len << 8) | (uint64_t)data[2 + i];
    header_size += 8;
  }

  if (data[1] & 0x80)
    header_size += 4; // Masking key

  if (payload_len > (uint64_t)(SIZE_MAX - header_size))
    return -1; // Prevent overflow

  size_t total = header_size + (size_t)payload_len;
  if (total > len)
    return 0; // Not enough data yet

  *frame_size = total;
  return 1;
}

static int ws_server_send_immediate_frame(ws_client_t *client, wsFrameType type, const uint8_t *payload, size_t payload_len)
{
  if (!client || client->fd < 0)
    return -1;

  size_t out_len = payload_len + 16;
  uint8_t *out_buf = malloc(out_len);
  if (!out_buf)
    return -1;

  ws_frame_t frame = {0};
  frame.type = type;
  frame.payload = (uint8_t *)payload;
  frame.payload_length = payload_len;

  ws_create_frame(&frame, out_buf, &out_len);
  (void)send(client->fd, out_buf, out_len, MSG_NOSIGNAL);
  free(out_buf);
  return 0;
}

static int ws_server_queue_frame_locked(ws_server_t *server, ws_client_t *client, wsFrameType type, const uint8_t *payload, size_t payload_len)
{
  if (!server || !client || client->fd < 0 || !client->active || !client->handshake_done)
    return -1;

  size_t tmp_len = payload_len + 16;
  uint8_t *tmp_buf = malloc(tmp_len);
  if (!tmp_buf)
    return -1;

  ws_frame_t frame = {0};
  frame.type = type;
  frame.payload = (uint8_t *)payload;
  frame.payload_length = payload_len;

  ws_create_frame(&frame, tmp_buf, &tmp_len);

  if (ws_ensure_capacity(&client->out_buf, &client->out_cap, client->out_len + tmp_len, server->initial_buffer_size) < 0)
  {
    free(tmp_buf);
    return -1;
  }

  memcpy(client->out_buf + client->out_len, tmp_buf, tmp_len);
  client->out_len += tmp_len;
  free(tmp_buf);

  if (ws_server_mod_client_events(server, client, EPOLLIN | EPOLLOUT | EPOLLRDHUP) < 0)
    return -1;

  return 0;
}

static void ws_server_close_client_now(ws_server_t *server, ws_client_t *client, const char *reason)
{
  if (!server || !client)
    return;

  int fd = -1;
  bool had_handshake = false;

  pthread_mutex_lock(&server->lock);
  if (!client->active)
  {
    pthread_mutex_unlock(&server->lock);
    return;
  }

  fd = client->fd;
  had_handshake = client->handshake_done;
  client->active = false;
  client->handshake_done = false;
  client->close_requested = false;
  client->fd = -1;
  pthread_mutex_unlock(&server->lock);

  if (fd >= 0)
  {
    (void)epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
  }

  if (had_handshake && server->on_disconnect)
    server->on_disconnect(server, client, reason ? reason : "Closed", server->user_data);

  ws_client_free_buffers(client);
  ws_client_reset_runtime(client);
}

static void ws_server_process_close_requests(ws_server_t *server)
{
  if (!server)
    return;

  for (int i = 0; i < server->max_clients; i++)
  {
    bool close_me = false;
    char reason[128] = {0};

    pthread_mutex_lock(&server->lock);
    if (server->clients[i].active && server->clients[i].close_requested)
    {
      close_me = true;
      snprintf(reason, sizeof(reason), "%s", server->clients[i].close_reason[0] ? server->clients[i].close_reason : "Close Requested");
      server->clients[i].close_requested = false;
    }
    pthread_mutex_unlock(&server->lock);

    if (close_me)
      ws_server_close_client_now(server, &server->clients[i], reason);
  }
}

static int ws_server_prepare_client(ws_server_t *server, ws_client_t *client, int fd, const struct sockaddr *addr, socklen_t addr_len)
{
  ws_client_free_buffers(client);
  ws_client_reset_runtime(client);

  if (ws_ensure_capacity(&client->in_buf, &client->in_cap, server->initial_buffer_size, server->initial_buffer_size) != 0)
    return -1;

  if (ws_ensure_capacity(&client->out_buf, &client->out_cap, server->initial_buffer_size, server->initial_buffer_size) != 0)
    return -1;

  client->fd = fd;
  client->active = true;
  client->handshake_done = false;
  client->close_requested = false;

  if (addr && addr_len <= sizeof(client->addr))
  {
    memcpy(&client->addr, addr, addr_len);
    client->addr_len = addr_len;
  }

  return 0;
}

static void ws_server_accept_new(ws_server_t *server)
{
  while (true)
  {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    int client_fd = accept(server->listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (client_fd < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      ws_server_report_error(server, "accept", errno);
      return;
    }

    if (ws_set_nonblocking(client_fd) < 0)
    {
      ws_server_report_error(server, "fcntl(O_NONBLOCK)", errno);
      close(client_fd);
      continue;
    }

    ws_client_t *slot = NULL;

    pthread_mutex_lock(&server->lock);
    for (int i = 0; i < server->max_clients; i++)
    {
      if (!server->clients[i].active && server->clients[i].fd == -1)
      {
        slot = &server->clients[i];
        break;
      }
    }
    pthread_mutex_unlock(&server->lock);

    if (!slot)
    {
      ws_server_report_error(server, "accept: no free client slots", 0);
      close(client_fd);
      continue;
    }

    if (ws_server_prepare_client(server, slot, client_fd, (struct sockaddr *)&addr, addr_len) != 0)
    {
      ws_server_report_error(server, "prepare_client", errno);
      close(client_fd);
      continue;
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.u64 = WS_TAG_PTR(slot);

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
    {
      ws_server_report_error(server, "epoll_ctl(ADD client)", errno);
      ws_client_free_buffers(slot);
      ws_client_reset_runtime(slot);
      close(client_fd);
    }
  }
}

static void ws_server_handle_handshake(ws_server_t *server, ws_client_t *client)
{
  size_t hdr_len = ws_find_http_header_t_end(client->in_buf, client->in_len);
  if (hdr_len == 0)
    return;

  size_t leftover_len = client->in_len - hdr_len;
  uint8_t *leftover = NULL;

  if (leftover_len > 0)
  {
    leftover = malloc(leftover_len);
    if (!leftover)
    {
      ws_server_report_error(server, "malloc(handshake leftover)", errno);
      ws_server_close_client_now(server, client, "Handshake Memory Error");
      return;
    }
    memcpy(leftover, client->in_buf + hdr_len, leftover_len);
  }

  http_header_t header = {0};

  size_t out_len = client->in_cap;
  if (ws_handshake(&header, client->in_buf, hdr_len, &out_len) < 0)
  {
    ws_server_report_error(server, "handshake", 0);
    free(leftover);
    ws_server_close_client_now(server, client, "Handshake Failed");
    return;
  }

  if (header.type == WS_OPENING_FRAME)
  {
    (void)send(client->fd, client->in_buf, out_len, MSG_NOSIGNAL);
    client->handshake_done = true;
    client->in_len = 0;

    if (leftover_len > 0)
    {
      if (ws_ensure_capacity(&client->in_buf, &client->in_cap, leftover_len, server->initial_buffer_size) != 0)
      {
        ws_server_report_error(server, "handshake leftover buffer", errno);
        free(leftover);
        ws_server_close_client_now(server, client, "Handshake Leftover Memory Error");
        return;
      }
      memcpy(client->in_buf, leftover, leftover_len);
      client->in_len = leftover_len;
    }

    if (server->on_connect)
      server->on_connect(server, client, server->user_data);
  }
  else
  {
    if (out_len > 0)
      (void)send(client->fd, client->in_buf, out_len, MSG_NOSIGNAL);

    if (server->on_handshake_fail)
      server->on_handshake_fail(server, client, "Invalid WebSocket Handshake", server->user_data);

    free(leftover);
    ws_server_close_client_now(server, client, "Invalid Handshake");
    return;
  }

  free(leftover);
}

static void ws_client_reset_fragments(ws_client_t *client)
{
  if (!client)
    return;

  client->frag_active = false;
  client->frag_type = WS_EMPTY_FRAME;
  client->frag_len = 0;
}

static int ws_client_append_fragment(ws_client_t *client, const uint8_t *data, size_t len)
{
  if (!client || !client)
    return -1;
  
  if (ws_ensure_capacity(&client->frag_buf, &client->frag_cap, client->frag_len + len + 1, server->initial_buffer_size) != 0)
    return -1;

  if (data && len > 0)
    memcpy(client->frag_buf + client->frag_len, data, len);
  
  client->frag_len += len;
  client->frag_buf[client->frag_len] = '\0';
  return 0;
}

static void ws_server_handle_frames(ws_server_t *server, ws_client_t *client)
{
  while (client->in_len > 0)
  {
    size_t frame_wire_size = 0;
    int rc = ws_peak_frame_wire_size(client->in_buf, client->in_len, &frame_wire_size);
    if (rc == 0)
      return;
    if (rc < 0)
    {
      ws_server_report_error(server, "peak_frame_wire_size", 0);
      ws_server_close_client_now(server, client, "Invalid Frame Size");
      return;
    }

    ws_frame_t frame = {0};
    ws_parse_frame(&frame, client->in_buf, frame_wire_size);

    if (frame.type == WS_INCOMPLETE_FRAME)
      return;

    if (frame.type == WS_ERROR_FRAME)
    {
      ws_server_report_error(server, "parse_frame", 0);
      ws_server_close_client_now(server, client, "Protocol Error");
      return;
    }

    switch (frame.type)
    {
    case WS_TEXT_FRAME:
    case WS_BINARY_FRAME:
      if (client->frag_active)
      {
        ws_server_report_error(server, "Non continuation frame while fragment is active", 0);
        ws_server_close_client_now(server, client, "Protocol Error - Unexpected new data frame during fragmentation");
        return;
      }

      if (frame.fin)
      {
        if (server->on_message)
          server->on_message(server, client, &frame, server->user_data);
      }
      else
      {
        client->frag_active = true;
        client->frag_type = frame.type;
        client->frag_len = 0;

        if (ws_client_append_fragment(client, frame.payload, frame.payload_length) != 0)
        {
          ws_server_report_error(server, "append_fragment", errno);
          ws_server_close_client_now(server, client, "Fragmentation Memory Error");
          return;
        }
      }
      break;
    case WS_CONTINUATION_FRAME:
      if (!client->frag_active)
      {
        ws_server_report_error(server, "Continuation frame while no fragment is active", 0);
        ws_server_close_client_now(server, client, "Protocol Error - Unexpected continuation frame");
        return;
      }

      if (ws_client_append_fragment(client, frame.payload, frame.payload_length) != 0)
      {
        ws_server_report_error(server, "append_fragment", errno);
        ws_server_close_client_now(server, client, "Fragmentation Memory Error");
        return;
      }

      if (frame.fin)
      {
        ws_frame_t assembled;
        assembled.fin = true;
        assembled.opcode = (uint8_t)client->frag_type;
        assembled.type = client->frag_type;
        assembled.payload = client->frag_buf;
        assembled.payload_length = client->frag_len;

        if (server->on_message)
          server->on_message(server, client, &assembled, server->user_data);
        ws_client_reset_fragments(client);
      }
      break;
    case WS_PING_FRAME:
      (void)ws_server_send_immediate_frame(client, WS_PONG_FRAME, frame.payload, frame.payload_length);
      break;
    case WS_CLOSING_FRAME:
      (void)ws_server_send_immediate_frame(client, WS_CLOSING_FRAME, frame.payload, frame.payload_length);
      ws_server_close_client_now(server, client, "Closed by Client");
      return;
    default:
      ws_server_close_client_now(server, client, "Unsupported Frame Type");
      return;
    }

    if (frame_wire_size < client->in_len)
      memmove(client->in_buf, client->in_buf + frame_wire_size, client->in_len - frame_wire_size);
    client->in_len -= frame_wire_size;
  }
}

static void ws_server_handle_client_read(ws_server_t *server, ws_client_t *client)
{
  if (!server || !client || !client->active || client->fd < 0)
    return;

  while (true)
  {
    uint8_t tmp[4096];
    ssize_t n = recv(client->fd, tmp, sizeof(tmp), 0);
    if (n < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      ws_server_report_error(server, "recv", errno);
      ws_server_close_client_now(server, client, "Read Error");
      return;
    }

    if (n == 0)
    {
      ws_server_close_client_now(server, client, "Client Disconnected");
      return;
    }

    if (ws_ensure_capacity(&client->in_buf, &client->in_cap, client->in_len + (size_t)n, server->initial_buffer_size) != 0)
    {
      ws_server_report_error(server, "recv buffer", errno);
      ws_server_close_client_now(server, client, "Receive Buffer Error");
      return;
    }

    memcpy(client->in_buf + client->in_len, tmp, (size_t)n);
    client->in_len += (size_t)n;

    if (!client->handshake_done)
    {
      ws_server_handle_handshake(server, client);
      if (!client->active)
        return;
    }

    if (client->handshake_done)
    {
      ws_server_handle_frames(server, client);
      if (!client->active)
        return;
    }
  }
}

static void ws_server_handle_client_write(ws_server_t *server, ws_client_t *client)
{
  if (!server || !client)
    return;

  while (true)
  {
    pthreads_mutex_lock(&server->lock);

    if (!client->active || client->fd < 0 || client->out_len == 0)
    {
      pthread_mutex_unlock(&server->lock);
      return;
    }

    if (client->out_sent >= client->out_len)
    {
      client->out_len = 0;
      client->out_sent = 0;
      (void)ws_server_mod_client_events(server, client, EPOLLIN | EPOLLET | EPOLLRDHUP);
      pthread_mutex_unlock(&server->lock);
      return;
    }

    size_t n = send(client->fd, client->out_buf + client->out_sent, client->out_len - client->out_sent, MSG_NOSIGNAL);
    if (n < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        pthread_mutex_unlock(&server->lock);
        return;
      }
      ws_server_report_error(server, "send", errno);
      pthread_mutex_unlock(&server->lock);
      ws_server_close_client_now(server, client, "Write Error");
      return;
    }

    client->out_sent += (size_t)n;
    pthread_mutex_unlock(&server->lock);
  }
}

static int ws_server_setup(ws_server_t *server)
{
  if (!server)
    return -1;

  server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->listen_fd < 0)
  {
    ws_server_report_error(server, "socket", errno);
    return -1;
  }

  int opt = 1;
  if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    ws_server_report_error(server, "setsockopt(SO_REUSEADDR)", errno);
    close(server->listen_fd);
    server->listen_fd = -1;
    return -1;
  }

  if (ws_set_nonblocking(server->listen_fd) < 0)
  {
    ws_server_report_error(server, "fcntl(O_NONBLOCK)", errno);
    close(server->listen_fd);
    server->listen_fd = -1;
    return -1;
  }

  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons((uint16_t)server->port);

  if (inet_pton(AF_INET, server->bind_addr, &server_addr.sin_addr) != 1)
  {
    close(server->listen_fd);
    errno = EINVAL;
    server->listen_fd = -1;
    ws_server_report_error(server, "inet_pton", errno);
    return -1;
  }

  if (bind(server->listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    ws_server_report_error(server, "bind", errno);
    close(server->listen_fd);
    server->listen_fd = -1;
    return -1;
  }

  if (listen(server->listen_fd, server->backlog) < 0)
  {
    ws_server_report_error(server, "listen", errno);
    close(server->listen_fd);
    server->listen_fd = -1;
    return -1;
  }

  server->epoll_fd = epoll_create1(0);
  if (server->epoll_fd < 0)
  {
    ws_server_report_error(server, "epoll_create1", errno);
    close(server->listen_fd);
    server->listen_fd = -1;
    return -1;
  }

  server->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (server->wake_fd < 0)
  {
    ws_server_report_error(server, "eventfd", errno);
    close(server->listen_fd);
    close(server->epoll_fd);
    server->listen_fd = -1;
    server->epoll_fd = -1;
    return -1;
  }

  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.u64 = WS_TAG_FD(server->listen_fd);
  if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &ev) < 0)
  {
    ws_server_report_error(server, "epoll_ctl(ADD listen_fd)", errno);
    close(server->listen_fd);
    close(server->epoll_fd);
    close(server->wake_fd);
    server->listen_fd = -1;
    server->epoll_fd = -1;
    server->wake_fd = -1;
    return -1;
  }

  memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.u64 = WS_TAG_FD(server->wake_fd);
  if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->wake_fd, &ev) < 0)
  {
    ws_server_report_error(server, "epoll_ctl(ADD wake_fd)", errno);
    close(server->listen_fd);
    close(server->epoll_fd);
    close(server->wake_fd);
    server->listen_fd = -1;
    server->epoll_fd = -1;
    server->wake_fd = -1;
    return -1;
  }

  return 0;
}

static void ws_server_teardown(ws_server_t *server)
{
  if (!server)
    return;

  if (server->listen_fd >= 0)
  {
    close(server->listen_fd);
    server->listen_fd = -1;
  }

  if (server->epoll_fd >= 0)
  {
    close(server->epoll_fd);
    server->epoll_fd = -1;
  }

  if (server->wake_fd >= 0)
  {
    close(server->wake_fd);
    server->wake_fd = -1;
  }
}

static void ws_server_close_all(ws_server_t *server, const char *reason)
{
  if (!server)
    return;

  for (int i = 0; i < server->max_clients; i++)
  {
    if (server->clients[i].active && server->clients[i].handshake_done)
      (void)ws_server_send_immediate_frame(&server->clients[i], WS_CLOSING_FRAME, NULL, 0);
    ws_server_close_client_now(server, &server->clients[i], reason ? reason : "Server Shutdown");
  }
}

static void *ws_server_thread_main(void *arg)
{
  ws_server_t *server = (ws_server_t *)arg;
  if (!server)
    return NULL;
  (void)ws_server_run(server);
  return NULL;
}

/* Public API Functions */

ws_server_t *ws_server_create(int port, int max_clients)
{
  if (port <= 0 || port > 65535 || max_clients <= 0)
    return NULL;

  ws_server_t *server = calloc(1, sizeof(ws_server_t));
  if (!server)
    return NULL;

  server->port = port;
  server->max_clients = max_clients;
  server->backlog = (max_clients > WS_SERVER_DEFAULT_BACKLOG) ? max_clients : WS_SERVER_DEFAULT_BACKLOG;
  server->max_events = WS_SERVER_DEFAULT_MAX_EVENTS;
  server->initial_buffer_size = WS_SERVER_DEFAULT_BUF_SIZE;
  server->listen_fd = -1;
  server->epoll_fd = -1;
  server->wake_fd = -1;
  server->started_as_thread = false;
  server->thead_joinable = false;
  server->running = false;

  snprintf(server->bind_addr, sizeof(server->bind_addr), "%s", WS_SERVER_DEFAULT_BIND_ADDR);

  if (pthread_mutex_init(&server->lock, NULL) != 0)
  {
    free(server);
    return NULL;
  }

  server->clients = calloc((size_t)max_clients, sizeof(ws_client_t));
  if (!server->clients)
  {
    pthread_mutex_destroy(&server->lock);
    free(server);
    return NULL;
  }

  for (int i = 0; i < max_clients; i++)
    ws_client_zero(&server->clients[i]);

  return server;
}

void ws_server_destroy(ws_server_t *server)
{
  if (!server)
    return;

  (void)ws_server_stop(server);
  (void)ws_server_join(server);

  ws_server_close_all(server, "Server Destroyed");
  ws_server_teardown(server);

  if (server->clients)
  {
    for (int i = 0; i < server->max_clients; i++)
      ws_client_free_buffers(&server->clients[i]);
    free(server->clients);
  }

  pthread_mutex_destroy(&server->lock);
  free(server);
}

int ws_server_set_bind_address(ws_server_t *server, const char *ip)
{
  if (!server || !ip || *ip == '\0')
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }

  if (inet_pton(AF_INET, ip, &(struct in_addr)){0} != 1)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  snprintf(server->bind_addr, sizeof(server->bind_addr), "%s", ip);
  pthread_mutex_unlock(&server->lock);
  return 0;
}

int ws_server_set_backlog(ws_server_t *server, int backlog)
{
  if (!server || backlog <= 0)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  server->backlog = backlog;
  pthread_mutex_unlock(&server->lock);
  return 0;
}

int ws_server_set_initial_buffer_size(ws_server_t *server, size_t bytes)
{
  if (!server || bytes == 0 || bytes > SIZE_MAX / 2 || bytes < 256)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  server->initial_buffer_size = bytes;
  pthread_mutex_unlock(&server->lock);
  return 0;
}

int ws_server_set_max_events(ws_server_t *server, int max_events)
{
  if (!server || max_events <= 0)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  server->max_events = max_events;
  pthread_mutex_unlock(&server->lock);
  return 0;
}

void ws_server_set_connect_handler(ws_server_t *server, ws_server_connect_handler_t handler)
{
  if (server)
    server->on_connect = handler;
}

void ws_server_set_message_handler(ws_server_t *server, ws_server_message_handler_t handler)
{
  if (server)
    server->on_message = handler;
}

void ws_server_set_disconnect_handler(ws_server_t *server, ws_server_disconnect_handler_t handler)
{
  if (server)
    server->on_disconnect = handler;
}

void ws_server_set_handshake_fail_handler(ws_server_t *server, ws_server_handshake_fail_handler_t handler)
{
  if (server)
    server->on_handshake_fail = handler;
}

void ws_server_set_error_handler(ws_server_t *server, ws_server_error_handler_t handler)
{
  if (server)
    server->on_error = handler;
}

void ws_server_set_user_data(ws_server_t *server, void *user_data)
{
  if (server)
    server->user_data = user_data;
}

void *ws_server_get_user_data(ws_server_t *server)
{
  return server ? server->user_data : NULL;
}

void ws_client_set_user_data(ws_client_t *client, void *user_data)
{
  if (client)
    client->user_data = user_data;
}

void *ws_client_get_user_data(ws_client_t *client)
{
  return client ? client->user_data : NULL;
}

int ws_server_run(ws_server_t *server)
{
  if (!server)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  pthread_mutex_unlock(&server->lock);

  if (ws_server_setup(server) != 0)
    return -1;

  ws_server_set_running(server, true);

  struct epoll_event *events = calloc((size_t)server->max_events, sizeof(struct epoll_event));
  if (!events)
  {
    ws_server_set_running(server, false);
    ws_server_teardown(server);
    return -1;
  }

  while (ws_server_is_running(server))
  {
    int n = epoll_wait(server->epoll_fd, events, server->max_events, -1);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      ws_server_report_error(server, "epoll_wait", errno);
      break;
    }

    for (int i = 0; i < n; i++)
    {
      uint64_t tag = events[i].data.u64;

      if (!WS_TAG_IS_PTR(tag))
      {
        int fd = WS_TAG_TO_FD(tag);
        if (fd == server->listen_fd)
          ws_server_accept_new(server);
        else if (fd == server->wake_fd)
        {
          uint64_t tmp;
          while (read(server->wake_fd, &tmp, sizeof(uint64_t)) == sizeof(uint64_t))
            ;
        }

        continue;
      }

      ws_client_t *client = (ws_client_t *)WS_TAG_TO_PTR(tag);
      if (!client || !client->active)
        continue;

      if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
      {
        ws_server_close_client_now(server, client, "Socket Hangup/Error");
        continue;
      }

      if (events[i].events & EPOLLIN)
      {
        ws_server_handle_client_read(server, client);
        if (!client->active)
          continue;
      }
    }

    ws_server_process_close_requests(server);
  }

  free(events);
  ws_server_close_all(server, "Server Stopped");
  ws_server_teardown(server);
  ws_server_set_running(server, false);
  return 0;
}

int ws_server_start(ws_server_t *server)
{
  if (!server)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (server->running || server->thead_joinable)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  server->started_as_thread = true;
  pthread_mutex_unlock(&server->lock);

  if (pthread_create(&server->thead, NULL, ws_server_thread_main, server) != 0)
  {
    pthread_mutex_lock(&server->lock);
    server->started_as_thread = false;
    pthread_mutex_unlock(&server->lock);
    return -1;
  }

  pthread_mutex_lock(&server->lock);
  server->thead_joinable = true;
  pthread_mutex_unlock(&server->lock);
  return 0;
}

int ws_server_stop(ws_server_t *server)
{
  if (!server)
    return -1

        ws_server_set_running(server, false);
  ws_server_wake(server);
  return 0;
}

int ws_server_join(ws_server_t *server)
{
  if (!server)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (!server->started_as_thread || !server->thead_joinable)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  pthread_mutex_unlock(&server->lock);

  if (pthread_join(server->thead, NULL) != 0)
    return -1;

  pthread_mutex_lock(&server->lock);
  server->started_as_thread = false;
  server->thead_joinable = false;
  pthread_mutex_unlock(&server->lock);
  return 0;
}

int ws_server_send_frame(ws_server_t *server, ws_client_t *client, wsFrameType type, const uint8_t *payload, size_t payload_len)
{
  if (!server || !client)
    return -1;

  int rc;
  pthread_mutex_lock(&server->lock);
  rc = ws_server_queue_frame_locked(server, client, type, payload, payload_len);
  pthread_mutex_unlock(&server->lock);

  if (rc == 0)
    ws_server_wake(server);

  return rc;
}

int ws_server_send_text(ws_server_t *server, ws_client_t *client, const char *text)
{
  if (!text)
    return -1;
  return ws_server_send_frame(server, client, WS_TEXT_FRAME, (const uint8_t *)text, strlen(text));
}

int ws_server_send_binary(ws_server_t *server, ws_client_t *client, const uint8_t *data, size_t data_len)
{
  if (!data || data_len == 0)
    return -1;
  return ws_server_send_frame(server, client, WS_BINARY_FRAME, data, data_len);
}

int ws_server_send_ping(ws_server_t *server, ws_client_t *client)
{
  return ws_server_send_frame(server, client, WS_PING_FRAME, NULL, 0);
}

int ws_server_send_pong(ws_server_t *server, ws_client_t *client)
{
  return ws_server_send_frame(server, client, WS_PONG_FRAME, NULL, 0);
}

int ws_server_broadcast_frame(ws_server_t *server, wsFrameType type, const uint8_t *payload, size_t payload_len)
{
  if (!server)
    return -1;

  int sent = 0;
  pthread_mutex_lock(&server->lock);
  for (int i = 0; i < server->max_clients; i++)
  {
    if (server->clients[i].active && server->clients[i].handshake_done)
    {
      if (ws_server_queue_frame_locked(server, &server->clients[i], type, payload, payload_len) == 0)
        sent++;
    }
  }
  pthread_mutex_unlock(&server->lock);

  if (sent > 0)
    ws_server_wake(server);

  return sent;
}

int ws_server_broadcast_text(ws_server_t *server, const char *text)
{
  if (!text)
    return -1;
  return ws_server_broadcast_frame(server, WS_TEXT_FRAME, (const uint8_t *)text, strlen(text));
}

int ws_server_broadcast_binary(ws_server_t *server, const uint8_t *data, size_t data_len)
{
  if (!data || data_len == 0)
    return -1;
  return ws_server_broadcast_frame(server, WS_BINARY_FRAME, data, data_len);
}

int ws_server_broadcast_ping(ws_server_t *server)
{
  return ws_server_broadcast_frame(server, WS_PING_FRAME, NULL, 0);
}

int ws_server_close_client(ws_server_t *server, ws_client_t *client, const char *reason)
{
  if (!server || !client)
    return -1;

  pthread_mutex_lock(&server->lock);
  if (!client->active || client->fd < 0)
  {
    pthread_mutex_unlock(&server->lock);
    return -1;
  }
  client->close_requested = true;
  snprintf(client->close_reason, sizeof(client->close_reason), "%s", reason ? reason : "Close Requested");
  pthread_mutex_unlock(&server->lock);

  ws_server_wake(server);
  return 0;
}

int ws_client_fd(ws_client_t *client)
{
  return client ? client->fd : -1;
}

bool ws_client_is_connected(ws_client_t *client)
{
  return client ? client->active : false;
}

bool ws_client_handshake_done(ws_client_t *client)
{
  return client ? client->handshake_done : false;
}

const char *ws_client_ip(const ws_client_t *client, char *buf, size_t len)
{
  if (!client || !buf || len == 0)
    return NULL;

  buf[0] = '\0';

  if (client->addr.ss_family == AF_INET)
  {
    const struct sockaddr_in *sa = (const struct sockaddr_in *)&client->addr;
    return inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)len);
  }
  else if (client->addr.ss_family == AF_INET6)
  {
    const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)&client->addr;
    return inet_ntop(AF_INET6, &sa6->sin6_addr, buf, (socklen_t)len);
  }

  return NULL;
}

uint16_t ws_client_port(const ws_client_t *client)
{
  if (!client)
    return 0;

  if (client->addr.ss_family == AF_INET)
  {
    const struct sockaddr_in *sa = (const struct sockaddr_in *)&client->addr;
    return ntohs(sa->sin_port);
  }
  else if (client->addr.ss_family == AF_INET6)
  {
    const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)&client->addr;
    return ntohs(sa6->sin6_port);
  }

  return 0;
}

size_t ws_server_client_count(ws_server_t *server)
{
  if (!server)
    return 0;

  size_t count = 0;
  pthread_mutex_lock(&server->lock);
  for (int i = 0; i < server->max_clients; i++)
  {
    if (server->clients[i].active && server->clients[i].handshake_done)
      count++;
  }
  pthread_mutex_unlock(&server->lock);
  return count;
}