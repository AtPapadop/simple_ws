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

static size_t ws_find_http_header_end(uint8_t *buf, size_t len)
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

  ws_frame frame = {0};
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
  
  ws_frame frame = {0};
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