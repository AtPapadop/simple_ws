/* wsclient.c - websocket client API
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

#include "simple_ws/wsclient.h"
#include "simple_ws/wshandshake.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WS_CLIENT_RECV_BUFFER_INITIAL 4096
#define WS_CLIENT_MAX_HANDSHAKE_BYTES 16384
#define WS_CLIENT_MASK_LEN 4

struct ws_remote_client
{
	int fd;
	bool connected;
	uint8_t *recv_buf;
	size_t recv_len;
	size_t recv_cap;
};

static uint16_t ws_to_network_u16(uint16_t v)
{
	return (uint16_t)((v >> 8) | (v << 8));
}

static bool ws_is_control_frame_type(wsFrameType type)
{
	return type == WS_CLOSING_FRAME || type == WS_PING_FRAME || type == WS_PONG_FRAME;
}

static bool ws_contains_upgrade_token(const char *value)
{
	if (!value)
		return false;

	const char *p = value;
	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if (*p == '\0')
			break;

		const char *start = p;
		while (*p && *p != ',')
			p++;

		size_t token_len = (size_t)(p - start);
		while (token_len > 0 && (start[token_len - 1] == ' ' || start[token_len - 1] == '\t'))
			token_len--;

		if (token_len == strlen("Upgrade") && strncasecmp(start, "Upgrade", token_len) == 0)
			return true;
	}

	return false;
}

static int ws_ensure_capacity(uint8_t **buf, size_t *cap, size_t needed, size_t initial)
{
	if (!buf || !cap)
		return -1;
	if (*cap >= needed)
		return 0;

	size_t new_cap = (*cap == 0) ? initial : *cap;
	if (new_cap == 0)
		new_cap = WS_CLIENT_RECV_BUFFER_INITIAL;

	while (new_cap < needed)
	{
		if (new_cap > SIZE_MAX / 2)
			return -1;
		new_cap *= 2;
	}

	uint8_t *tmp = realloc(*buf, new_cap);
	if (!tmp)
		return -1;
	*buf = tmp;
	*cap = new_cap;
	return 0;
}

static int ws_poll_fd(int fd, short events, int timeout_ms)
{
	struct pollfd pfd = {0};
	pfd.fd = fd;
	pfd.events = events;

	int rc = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : -1);
	if (rc <= 0)
		return rc;

	if ((pfd.revents & POLLERR) || (pfd.revents & POLLHUP) || (pfd.revents & POLLNVAL))
	{
		errno = ECONNRESET;
		return -1;
	}
	return 1;
}

static int ws_send_all(int fd, const uint8_t *data, size_t len, int timeout_ms)
{
	size_t sent = 0;
	while (sent < len)
	{
		int wait_rc = ws_poll_fd(fd, POLLOUT, timeout_ms);
		if (wait_rc <= 0)
		{
			if (wait_rc == 0)
				errno = ETIMEDOUT;
			return -1;
		}

		ssize_t n = send(fd, data + sent, len - sent, 0);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return -1;
		}
		if (n == 0)
		{
			errno = ECONNRESET;
			return -1;
		}
		sent += (size_t)n;
	}
	return 0;
}

static int ws_generate_nonce(uint8_t *nonce, size_t len)
{
	if (!nonce || len == 0)
		return -1;

	static bool seeded = false;
	if (!seeded)
	{
		srand((unsigned int)(time(NULL) ^ getpid()));
		seeded = true;
	}

	for (size_t i = 0; i < len; i++)
		nonce[i] = (uint8_t)(rand() & 0xFF);

	return 0;
}

static int ws_make_client_handshake_request(char *request, size_t request_len,
		const char *host, uint16_t port, const char *path, const char *origin,
		char *out_key, size_t out_key_len)
{
	if (!request || !host || !path || !out_key)
		return -1;

	uint8_t nonce[16];
	size_t key_len = out_key_len;
	if (ws_generate_nonce(nonce, sizeof(nonce)) < 0)
		return -1;
	base64_encode(nonce, sizeof(nonce), out_key, &key_len);

	int written;
	if (origin && origin[0] != '\0')
	{
		written = snprintf(request, request_len,
						   "GET %s HTTP/1.1\r\n"
						   "Host: %s:%u\r\n"
						   "Upgrade: websocket\r\n"
						   "Connection: Upgrade\r\n"
						   "Sec-WebSocket-Key: %s\r\n"
						   "Sec-WebSocket-Version: %d\r\n"
						   "Origin: %s\r\n"
						   "\r\n",
						   path, host, (unsigned int)port, out_key, WS_VERSION, origin);
	}
	else
	{
		written = snprintf(request, request_len,
						   "GET %s HTTP/1.1\r\n"
						   "Host: %s:%u\r\n"
						   "Upgrade: websocket\r\n"
						   "Connection: Upgrade\r\n"
						   "Sec-WebSocket-Key: %s\r\n"
						   "Sec-WebSocket-Version: %d\r\n"
						   "\r\n",
						   path, host, (unsigned int)port, out_key, WS_VERSION);
	}

	if (written <= 0 || (size_t)written >= request_len)
		return -1;
	return written;
}

static size_t ws_find_header_end(const uint8_t *buf, size_t len)
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

static const char *ws_trim_left(const char *s)
{
	if (!s)
		return s;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static int ws_validate_handshake_response(const uint8_t *buf, size_t len, const char *request_key)
{
	if (!buf || !request_key || len == 0)
		return -1;

	char expected_accept[64] = {0};
	size_t expected_len = sizeof(expected_accept);
	if (ws_make_accept_key(request_key, expected_accept, &expected_len) <= 0)
		return -1;

	char *headers = malloc(len + 1);
	if (!headers)
		return -1;
	memcpy(headers, buf, len);
	headers[len] = '\0';

	char *saveptr = NULL;
	char *line = strtok_r(headers, "\r\n", &saveptr);
	if (!line)
	{
		free(headers);
		return -1;
	}

	if (strncmp(line, "HTTP/1.1 101", 12) != 0 && strncmp(line, "HTTP/1.0 101", 12) != 0)
	{
		free(headers);
		errno = EPROTO;
		return -1;
	}

	bool has_upgrade = false;
	bool has_connection = false;
	bool has_accept = false;

	while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL)
	{
		char *colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = '\0';
		const char *name = line;
		const char *value = ws_trim_left(colon + 1);

		if (strcasecmp(name, "Upgrade") == 0 && strcasecmp(value, "websocket") == 0)
			has_upgrade = true;
		else if (strcasecmp(name, "Connection") == 0 && ws_contains_upgrade_token(value))
			has_connection = true;
		else if (strcasecmp(name, "Sec-WebSocket-Accept") == 0 && strcmp(value, expected_accept) == 0)
			has_accept = true;
	}

	free(headers);
	if (!has_upgrade || !has_connection || !has_accept)
	{
		errno = EPROTO;
		return -1;
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
		header_size += WS_CLIENT_MASK_LEN;

	if (payload_len > (uint64_t)(SIZE_MAX - header_size))
		return -1;

	size_t total = header_size + (size_t)payload_len;
	if (total > len)
		return 0;

	*frame_size = total;
	return 1;
}

static int ws_recv_into_buffer(ws_remote_client_t *client, int timeout_ms)
{
	if (!client || client->fd < 0)
		return -1;

	int wait_rc = ws_poll_fd(client->fd, POLLIN, timeout_ms);
	if (wait_rc <= 0)
	{
		if (wait_rc == 0)
			errno = ETIMEDOUT;
		return -1;
	}

	uint8_t tmp[4096];
	ssize_t n = recv(client->fd, tmp, sizeof(tmp), 0);
	if (n < 0)
	{
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	if (n == 0)
	{
		errno = ECONNRESET;
		return -1;
	}

	if (ws_ensure_capacity(&client->recv_buf, &client->recv_cap, client->recv_len + (size_t)n, WS_CLIENT_RECV_BUFFER_INITIAL) < 0)
		return -1;

	memcpy(client->recv_buf + client->recv_len, tmp, (size_t)n);
	client->recv_len += (size_t)n;
	return 0;
}

static int ws_create_masked_frame(wsFrameType type, bool fin, const uint8_t *payload, size_t payload_len,
		uint8_t *out_data, size_t *out_len)
{
	if (!out_data || !out_len)
		return -1;

	size_t header_len = 2;
	out_data[0] = (uint8_t)((fin ? 0x80 : 0x00) | ((uint8_t)type & 0x0F));

	if (payload_len <= 0x7D)
	{
		out_data[1] = (uint8_t)(0x80 | payload_len);
	}
	else if (payload_len <= 0xFFFF)
	{
		out_data[1] = 0x80 | 0x7E;
		out_data[2] = (uint8_t)((payload_len >> 8) & 0xFF);
		out_data[3] = (uint8_t)(payload_len & 0xFF);
		header_len += 2;
	}
	else
	{
		out_data[1] = 0x80 | 0x7F;
		for (int i = 0; i < 8; i++)
			out_data[2 + i] = (uint8_t)((payload_len >> (56 - (i * 8))) & 0xFF);
		header_len += 8;
	}

	uint8_t mask_key[WS_CLIENT_MASK_LEN] = {0};
	if (ws_generate_nonce(mask_key, sizeof(mask_key)) < 0)
		return -1;

	memcpy(out_data + header_len, mask_key, sizeof(mask_key));
	header_len += sizeof(mask_key);

	for (size_t i = 0; i < payload_len; i++)
		out_data[header_len + i] = (uint8_t)(payload[i] ^ mask_key[i % WS_CLIENT_MASK_LEN]);

	*out_len = header_len + payload_len;
	return 0;
}

ws_remote_client_t *ws_remote_client_create(void)
{
	ws_remote_client_t *client = calloc(1, sizeof(*client));
	if (!client)
		return NULL;

	client->fd = -1;
	client->connected = false;
	return client;
}

void ws_remote_client_destroy(ws_remote_client_t *client)
{
	if (!client)
		return;

	if (client->fd >= 0)
		close(client->fd);

	free(client->recv_buf);
	free(client);
}

int ws_remote_client_connect(ws_remote_client_t *client, const char *host, uint16_t port, const char *path, const char *origin, int timeout_ms)
{
	if (!client || !host || !path)
	{
		errno = EINVAL;
		return -1;
	}

	if (client->connected)
	{
		errno = EISCONN;
		return -1;
	}

	char service[16] = {0};
	snprintf(service, sizeof(service), "%u", (unsigned int)port);

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *results = NULL;
	if (getaddrinfo(host, service, &hints, &results) != 0)
		return -1;

	int fd = -1;
	for (struct addrinfo *it = results; it; it = it->ai_next)
	{
		fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, it->ai_addr, it->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}

	freeaddrinfo(results);

	if (fd < 0)
		return -1;

	char request[1024] = {0};
	char key[64] = {0};
	int request_len = ws_make_client_handshake_request(request, sizeof(request), host, port, path, origin, key, sizeof(key));
	if (request_len < 0)
	{
		close(fd);
		return -1;
	}

	if (ws_send_all(fd, (const uint8_t *)request, (size_t)request_len, timeout_ms) < 0)
	{
		close(fd);
		return -1;
	}

	uint8_t *handshake = malloc(WS_CLIENT_MAX_HANDSHAKE_BYTES + 1);
	if (!handshake)
	{
		close(fd);
		return -1;
	}

	size_t handshake_len = 0;
	size_t header_end = 0;
	while (header_end == 0)
	{
		int wait_rc = ws_poll_fd(fd, POLLIN, timeout_ms);
		if (wait_rc <= 0)
		{
			if (wait_rc == 0)
				errno = ETIMEDOUT;
			free(handshake);
			close(fd);
			return -1;
		}

		ssize_t n = recv(fd, handshake + handshake_len, WS_CLIENT_MAX_HANDSHAKE_BYTES - handshake_len, 0);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			free(handshake);
			close(fd);
			return -1;
		}
		if (n == 0)
		{
			free(handshake);
			close(fd);
			errno = ECONNRESET;
			return -1;
		}

		handshake_len += (size_t)n;
		header_end = ws_find_header_end(handshake, handshake_len);
		if (handshake_len >= WS_CLIENT_MAX_HANDSHAKE_BYTES)
		{
			free(handshake);
			close(fd);
			errno = EOVERFLOW;
			return -1;
		}
	}

	if (ws_validate_handshake_response(handshake, header_end, key) < 0)
	{
		free(handshake);
		close(fd);
		return -1;
	}

	client->fd = fd;
	client->connected = true;

	size_t leftover = handshake_len - header_end;
	if (leftover > 0)
	{
		if (ws_ensure_capacity(&client->recv_buf, &client->recv_cap, leftover, WS_CLIENT_RECV_BUFFER_INITIAL) < 0)
		{
			free(handshake);
			ws_remote_client_disconnect(client);
			return -1;
		}
		memcpy(client->recv_buf, handshake + header_end, leftover);
		client->recv_len = leftover;
	}
	else
	{
		client->recv_len = 0;
	}

	free(handshake);
	return 0;
}

int ws_remote_client_disconnect(ws_remote_client_t *client)
{
	if (!client)
	{
		errno = EINVAL;
		return -1;
	}

	if (client->connected)
		(void)ws_remote_client_send_close(client, 1000, "Normal Closure");

	if (client->fd >= 0)
		close(client->fd);

	client->fd = -1;
	client->connected = false;
	client->recv_len = 0;
	return 0;
}

int ws_remote_client_send_frame(ws_remote_client_t *client, wsFrameType type, bool fin, const uint8_t *payload, size_t payload_len)
{
	if (!client || !client->connected || client->fd < 0)
	{
		errno = ENOTCONN;
		return -1;
	}

	if (payload_len > 0 && !payload)
	{
		errno = EINVAL;
		return -1;
	}

	if (ws_is_control_frame_type(type) && (!fin || payload_len > 125))
	{
		errno = EINVAL;
		return -1;
	}

	size_t frame_capacity = payload_len + 32;
	uint8_t *frame_buf = malloc(frame_capacity);
	if (!frame_buf)
		return -1;

	size_t frame_len = frame_capacity;
	if (ws_create_masked_frame(type, fin, payload, payload_len, frame_buf, &frame_len) < 0)
	{
		free(frame_buf);
		return -1;
	}

	int rc = ws_send_all(client->fd, frame_buf, frame_len, -1);
	free(frame_buf);
	return rc;
}

int ws_remote_client_send_text(ws_remote_client_t *client, const char *text)
{
	if (!text)
	{
		errno = EINVAL;
		return -1;
	}
	return ws_remote_client_send_frame(client, WS_TEXT_FRAME, true, (const uint8_t *)text, strlen(text));
}

int ws_remote_client_send_binary(ws_remote_client_t *client, const uint8_t *data, size_t len)
{
	if (len > 0 && !data)
	{
		errno = EINVAL;
		return -1;
	}
	return ws_remote_client_send_frame(client, WS_BINARY_FRAME, true, data, len);
}

int ws_remote_client_send_ping(ws_remote_client_t *client, const uint8_t *payload, size_t payload_len)
{
	return ws_remote_client_send_frame(client, WS_PING_FRAME, true, payload, payload_len);
}

int ws_remote_client_send_pong(ws_remote_client_t *client, const uint8_t *payload, size_t payload_len)
{
	return ws_remote_client_send_frame(client, WS_PONG_FRAME, true, payload, payload_len);
}

int ws_remote_client_send_close(ws_remote_client_t *client, uint16_t code, const char *reason)
{
	uint8_t payload[2 + 123] = {0};
	size_t reason_len = 0;
	if (reason && reason[0] != '\0')
	{
		reason_len = strlen(reason);
		if (reason_len > 123)
			reason_len = 123;
	}

	uint16_t net_code = ws_to_network_u16(code);
	memcpy(payload, &net_code, sizeof(net_code));
	if (reason_len > 0)
		memcpy(payload + 2, reason, reason_len);

	return ws_remote_client_send_frame(client, WS_CLOSING_FRAME, true, payload, 2 + reason_len);
}

int ws_remote_client_receive_frame(ws_remote_client_t *client, ws_frame_t *frame, uint8_t **frame_buf, size_t *frame_buf_len, int timeout_ms)
{
	if (!client || !frame || !frame_buf || !frame_buf_len)
	{
		errno = EINVAL;
		return -1;
	}

	if (!client->connected || client->fd < 0)
	{
		errno = ENOTCONN;
		return -1;
	}

	*frame_buf = NULL;
	*frame_buf_len = 0;

	while (true)
	{
		if (client->recv_len == 0)
		{
			if (ws_recv_into_buffer(client, timeout_ms) < 0)
			{
				if (errno == ETIMEDOUT)
					return -1;

				client->connected = false;
				close(client->fd);
				client->fd = -1;
				return -1;
			}
			continue;
		}

		size_t wire_size = 0;
		int peak = ws_peak_frame_wire_size(client->recv_buf, client->recv_len, &wire_size);
		if (peak < 0)
		{
			errno = EPROTO;
			return -1;
		}
		if (peak == 1)
		{
			uint8_t *copy = malloc(wire_size);
			if (!copy)
				return -1;

			memcpy(copy, client->recv_buf, wire_size);
			if (wire_size < client->recv_len)
				memmove(client->recv_buf, client->recv_buf + wire_size, client->recv_len - wire_size);
			client->recv_len -= wire_size;

			memset(frame, 0, sizeof(*frame));
			ws_parse_frame(frame, copy, wire_size);
			if (frame->type == WS_ERROR_FRAME || frame->type == WS_INCOMPLETE_FRAME)
			{
				free(copy);
				errno = EPROTO;
				return -1;
			}

			*frame_buf = copy;
			*frame_buf_len = wire_size;
			return 0;
		}

		if (ws_recv_into_buffer(client, timeout_ms) < 0)
		{
			if (errno == ETIMEDOUT)
				return -1;

			client->connected = false;
			close(client->fd);
			client->fd = -1;
			return -1;
		}
	}
}

void ws_remote_client_free_frame_buffer(uint8_t *frame_buf)
{
	free(frame_buf);
}

bool ws_remote_client_is_connected(const ws_remote_client_t *client)
{
	return client ? client->connected : false;
}

int ws_remote_client_fd(const ws_remote_client_t *client)
{
	if (!client)
		return -1;
	return client->fd;
}
