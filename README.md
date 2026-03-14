# simple_ws

A lightweight C WebSocket server library with a small API for building event-driven WebSocket services.

The project provides:
- WebSocket handshake parsing and response generation
- WebSocket frame parsing and frame construction
- A multi-client server with callbacks for connect/message/disconnect/error
- Helper modules for SHA-1 and Base64 used by the WebSocket protocol
- An echo server example

## Project layout

- `include/simple_ws/simple_ws.h`: umbrella header
- `include/simple_ws/wsserver.h`: high-level server API
- `include/simple_ws/websocket.h`: frame encode/decode API
- `include/simple_ws/wshandshake.h`: HTTP/WebSocket handshake helpers
- `include/simple_ws/sha1.h`: SHA-1 helpers
- `include/simple_ws/base64.h`: Base64 helpers
- `src/`: implementation
- `examples/echoserver.c`: runnable example server

## Requirements

- C compiler with C99 support
- CMake 3.16+
- pthread-compatible environment

Notes:
- The current implementation uses POSIX APIs (epoll, eventfd, pthread, socket APIs).
- It is intended for Linux-like environments.
- On Windows, build in a POSIX-compatible toolchain/environment (for example WSL or MinGW with compatible libraries).

## Build the library

### 1) Configure

```bash
cmake -S . -B build
```

### 2) Compile

```bash
cmake --build build
```

This produces the `simple_ws` library target.

## Install

```bash
cmake --install build --prefix /usr/local
```

Installed artifacts include:
- library files in `lib`
- headers in `include`
- CMake export files in `lib/cmake/simple_ws`

## Use the installed library (no -Lbuild)

If you installed to `/usr/local`, compile against the installed headers and library:

```bash
cc -std=c99 -I/usr/local/include examples/echoserver.c -L/usr/local/lib -lsimple_ws -lpthread -o echoserver
```

If you built a shared library and the loader does not find it at runtime, add an rpath:

```bash
cc -std=c99 -I/usr/local/include examples/echoserver.c -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lsimple_ws -lpthread -o echoserver
```

Or add `/usr/local/lib` to the dynamic linker cache (system-wide):

```bash
sudo ldconfig
```

## Use in your project

Include the umbrella header:

```c
#include <simple_ws/simple_ws.h>
```

Or include only the specific module headers you need.

## Core API overview

### Server lifecycle

- `ws_server_create(port, max_clients)`
- `ws_server_start(server)` or `ws_server_run(server)`
- `ws_server_stop(server)`
- `ws_server_join(server)`
- `ws_server_destroy(server)`

### Register callbacks

- `ws_server_set_connect_handler(...)`
- `ws_server_set_message_handler(...)`
- `ws_server_set_disconnect_handler(...)`
- `ws_server_set_handshake_fail_handler(...)`
- `ws_server_set_error_handler(...)`

### Send data

- To one client:
  - `ws_server_send_text(...)`
  - `ws_server_send_binary(...)`
  - `ws_server_send_ping(...)`
  - `ws_server_send_pong(...)`
  - `ws_server_send_frame(...)`
- Broadcast:
  - `ws_server_broadcast_text(...)`
  - `ws_server_broadcast_binary(...)`
  - `ws_server_broadcast_ping(...)`
  - `ws_server_broadcast_frame(...)`

### Client helpers

- `ws_client_fd(client)`
- `ws_client_is_connected(client)`
- `ws_client_handshake_done(client)`
- `ws_client_ip(client, buffer, len)`
- `ws_client_port(client)`

### Low-level protocol helpers

- Handshake:
  - `ws_handshake(...)`
  - `ws_make_accept_key(...)`
  - `ws_make_handshake(...)`
- Frame encode/decode:
  - `ws_parse_frame(...)`
  - `ws_create_frame(...)`
  - `ws_create_text_frame(...)`
  - `ws_create_binary_frame(...)`
  - `ws_create_control_frame(...)`
  - `ws_create_closing_frame(...)`
  - `ws_create_fragment(...)`

## Minimal server usage pattern

```c
#include <simple_ws/simple_ws.h>
#include <stdio.h>

static void on_message(ws_server_t *server, ws_client_t *client, const ws_frame_t *frame, void *user_data)
{
  (void)user_data;
  if (frame->type == WS_TEXT_FRAME)
    ws_server_send_text(server, client, (const char *)frame->payload);
}

int main(void)
{
  ws_server_t *server = ws_server_create(8888, 128);
  if (!server)
    return 1;

  ws_server_set_message_handler(server, on_message);

  if (ws_server_start(server) != 0)
  {
    ws_server_destroy(server);
    return 1;
  }

  /* In a real app, wait for your shutdown signal here. */
  ws_server_stop(server);
  ws_server_join(server);
  ws_server_destroy(server);
  return 0;
}
```

Important callback note:
- Frame payload memory provided in message callbacks is owned by the server and only valid during that callback.
- Copy payload data if you need to keep it after the callback returns.

## Build and run the example

The repository includes [examples/echoserver.c](examples/echoserver.c), which:
- accepts WebSocket clients
- sends a welcome message on connect
- echoes text and binary messages
- responds to ping with pong
- closes the client when it receives `quit`

### Build example manually (after building library)

```bash
gcc -std=c99 -Iinclude examples/echoserver.c -Lbuild -lsimple_ws -lpthread -o echoserver
```

If your build output directory differs, adjust `-Lbuild` accordingly.

### Run example

```bash
./echoserver 8888
```

Connect with a WebSocket client to:

```text
ws://127.0.0.1:8888
```

## CMake integration in another project

After installation, you can consume the exported target from your own CMake project:

```cmake
find_package(simple_ws REQUIRED)

target_link_libraries(your_target PRIVATE simple_ws::simple_ws)
```

## Threading and shutdown model

- `ws_server_run(...)` blocks in the current thread.
- `ws_server_start(...)` runs the event loop in an internal thread.
- Use `ws_server_stop(...)` to request shutdown.
- Call `ws_server_join(...)` after `ws_server_start(...)` to wait for clean exit.

## License

This project is distributed under the GNU General Public License (GPL), version 2 or later, as stated in the source headers.
