# Crux

> **A coroutine-native, high-performance backend runtime for real-time systems, built from the ground up in modern C++.**

Crux is an asynchronous backend runtime inspired by the architectural ideas behind **Node.js, libuv, Tokio, and modern coroutine-based runtimes**.

It combines a Linux `epoll`-based event loop with C++20 coroutines to provide an ergonomic, non-blocking programming model for building high-concurrency TCP, HTTP, and real-time services.

The project is designed around one central idea:

> **Make high-performance asynchronous networking feel simple without hiding the runtime mechanics.**

Crux is not a web framework sitting on top of an existing runtime. The runtime itself owns the event loop, I/O readiness, coroutine scheduling, timers, connection lifecycle, and protocol handling.

---

## Table of Contents

* [Overview](#overview)
* [Why Crux?](#why-crux)
* [Key Features](#key-features)
* [Architecture](#architecture)
* [Core Design](#core-design)

  * [Event Loop](#event-loop)
  * [Epoll](#epoll)
  * [Channels](#channels)
  * [Coroutines](#coroutines)
  * [Scheduler](#scheduler)
  * [Async I/O](#async-io)
  * [Timers](#timers)
  * [TCP](#tcp)
  * [HTTP](#http)
  * [WebSockets](#websockets)
* [Request Lifecycle](#request-lifecycle)
* [Concurrency Model](#concurrency-model)
* [Project Structure](#project-structure)
* [Requirements](#requirements)
* [Building](#building)
* [Running the Examples](#running-the-examples)
* [Example](#example)
* [API Overview](#api-overview)
* [Error Handling](#error-handling)
* [Memory Model](#memory-model)
* [Performance](#performance)
* [Benchmarking](#benchmarking)
* [Design Trade-offs](#design-trade-offs)
* [Crux vs Traditional Servers](#crux-vs-traditional-servers)
* [Roadmap](#roadmap)
* [Testing](#testing)
* [Debugging](#debugging)
* [Development](#development)
* [Contributing](#contributing)
* [License](#license)

---

# Overview

Traditional synchronous servers often model concurrency using one of two approaches:

```text
Connection
    │
    ▼
Thread
    │
    ▼
Blocking I/O
```

This model is straightforward, but thousands of concurrent connections can result in significant thread, memory, and context-switching overhead.

Crux instead uses an event-driven architecture:

```text
                    ┌──────────────────┐
                    │    Application   │
                    │      Code        │
                    └────────┬─────────┘
                             │
                       C++20 Coroutine
                             │
                             ▼
                    ┌──────────────────┐
                    │ Coroutine        │
                    │ Scheduler        │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │    Event Loop    │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │      epoll       │
                    └────────┬─────────┘
                             │
             ┌───────────────┼────────────────┐
             ▼               ▼                ▼
          Socket A        Socket B         Socket C
```

When an operation would block, the coroutine yields control.

The event loop continues processing other work.

When the operating system reports that the required I/O is ready, Crux resumes the suspended coroutine.

This gives application code a sequential programming style while retaining an event-driven execution model.

---

# Why Crux?

Modern asynchronous runtimes generally force developers to choose between:

1. **Low-level control**
2. **High-level ergonomics**

Crux attempts to provide both.

Instead of writing callback-heavy code:

```cpp
read(fd, buffer, ...,
    [](auto result) {
        write(fd, ...,
            [](auto result) {
                ...
            });
    });
```

Crux allows asynchronous code to be expressed using coroutines:

```cpp
auto handle_connection(Connection connection) -> Task<> {
    auto request = co_await connection.read();
    auto response = co_await handle_request(request);

    co_await connection.write(response);
}
```

The coroutine syntax hides continuation management while the runtime retains direct control over the underlying event-driven machinery.

---

# Key Features

### Runtime

* C++20 coroutine-based execution
* Linux `epoll` event loop
* Non-blocking socket I/O
* Coroutine scheduler
* Event-driven architecture
* Explicit ownership and lifecycle management
* RAII-based resource management

### Networking

* TCP server
* TCP client primitives
* Asynchronous accept/read/write
* Connection lifecycle management
* Non-blocking sockets
* HTTP server
* WebSocket support

### Performance

* No thread-per-connection model
* Minimal context switching
* Kernel-backed readiness notifications
* Coroutine suspension instead of blocking threads
* Efficient event dispatch
* Designed for high concurrent connection counts

### Developer Experience

* Sequential async code using `co_await`
* Strongly typed C++ APIs
* Composable async operations
* Clear separation between runtime and protocol layers
* Small core runtime

---

# Architecture

At a high level:

```text
┌──────────────────────────────────────────────────────────────┐
│                         Application                          │
│                                                              │
│   HTTP Handler      TCP Service      WebSocket Application   │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                       Protocol Layer                         │
│                                                              │
│        HTTP Parser       WebSocket       HTTP Server         │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                      Networking Layer                        │
│                                                              │
│       TCP Server       Connection       Socket Operations    │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                       Async Runtime                          │
│                                                              │
│      Task       Awaitables       Scheduler       Timers       │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                         Event Loop                            │
│                                                              │
│               Event Dispatch + Polling                       │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                         Linux Kernel                          │
│                                                              │
│              epoll      sockets      TCP/IP stack             │
└──────────────────────────────────────────────────────────────┘
```

The runtime is intentionally layered.

Protocol code does not need to understand `epoll`.

The event loop does not need to understand HTTP.

The coroutine scheduler does not need to understand TCP semantics.

Each layer has a narrow responsibility.

---

# Core Design

## Event Loop

The event loop is the heart of Crux.

Its primary responsibility is to:

1. Wait for operating-system I/O events.
2. Identify the resource associated with each event.
3. Dispatch the event.
4. Resume the appropriate coroutine/task.
5. Process timers and scheduled work.
6. Repeat until shutdown.

Conceptually:

```cpp
while (!stopping_) {
    process_ready_tasks();

    auto events = poller_.wait(timeout);

    for (const auto& event : events) {
        dispatch(event);
    }

    process_timers();
}
```

The loop avoids blocking on individual connections.

Instead, it waits for the operating system to tell it which resources are ready.

---

# Epoll

On Linux, Crux uses `epoll` as its primary I/O readiness mechanism.

The runtime initializes the poller using:

```cpp
epoll_create1(EPOLL_CLOEXEC);
```

Sockets are registered with the epoll instance and associated with the runtime's channel abstraction.

A typical flow looks like:

```text
Socket
   │
   ▼
epoll_ctl(ADD)
   │
   ▼
epoll_wait()
   │
   ▼
Ready events
   │
   ▼
Channel
   │
   ▼
Coroutine resumption
```

This allows the runtime to monitor a large number of file descriptors without continuously polling each socket individually.

---

# Channels

A `Channel` represents the runtime's interest in events associated with a file descriptor.

Conceptually:

```cpp
class Channel {
public:
    using EventCallback = std::function<void(const Event&)>;

private:
    int fd_;
    uint32_t events_;
    uint32_t revents_;

    EventCallback readCallback_;
    EventCallback writeCallback_;
};
```

A channel connects:

```text
file descriptor
       │
       ▼
    Channel
       │
       ▼
 event callbacks
       │
       ▼
 coroutine/task
```

This abstraction keeps the event loop independent from the concrete networking operation.

---

# Coroutines

Crux uses **C++20 coroutines** as its primary asynchronous programming abstraction.

A coroutine can suspend when an operation cannot immediately complete:

```cpp
auto handle() -> Task<> {
    auto data = co_await socket.read();

    auto result = process(data);

    co_await socket.write(result);
}
```

The important distinction is:

```text
Blocking model

Thread
  │
  └── waits ────────────────┐
                            │
                            ▼
                         I/O done


Crux

Coroutine
  │
  └── suspend
       │
       ▼
   Event Loop ──────────────┐
                            │
                            ▼
                       I/O becomes ready
                            │
                            ▼
                      Resume coroutine
```

The operating-system thread remains available to process other tasks while the coroutine is suspended.

---

# Scheduler

The scheduler is responsible for managing runnable coroutine work.

Its conceptual lifecycle is:

```text
Created
   │
   ▼
Runnable
   │
   ▼
Running
   │
   ├───────────────┐
   │               │
   ▼               ▼
Complete        Suspended
                   │
                   ▼
              I/O readiness
                   │
                   ▼
                Runnable
```

The scheduler does not continuously execute every coroutine.

Instead, suspended coroutines consume no CPU until an event causes them to become runnable again.

This is one of the fundamental advantages of the architecture.

---

# Async I/O

Crux exposes asynchronous I/O through awaitable operations.

For example:

```cpp
auto data = co_await connection.read();
```

Internally:

```text
co_await read()
       │
       ▼
Attempt read()
       │
       ├── data available
       │       │
       │       ▼
       │    return
       │
       └── EAGAIN
               │
               ▼
        Suspend coroutine
               │
               ▼
          Register interest
               │
               ▼
           epoll_wait()
               │
               ▼
          FD becomes ready
               │
               ▼
        Resume coroutine
               │
               ▼
          Retry read()
```

The complexity of this state machine is handled by the runtime rather than application code.

---

# Timers

Timers are integrated into the runtime rather than requiring application threads to sleep.

For example:

```cpp
co_await sleep_for(100ms);
```

Conceptually:

```text
Coroutine
    │
    ▼
sleep_for()
    │
    ▼
Suspend
    │
    ▼
Timer registered
    │
    ▼
Event loop continues
    │
    ▼
Timer expires
    │
    ▼
Coroutine scheduled
    │
    ▼
Resume
```

This allows thousands of delayed operations without requiring thousands of sleeping threads.

---

# TCP

The TCP layer provides asynchronous socket primitives.

A server typically follows:

```text
socket()
   │
   ▼
bind()
   │
   ▼
listen()
   │
   ▼
accept()
   │
   ▼
Connection
   │
   ├── read()
   ├── write()
   └── close()
```

Every connection is represented as a runtime-managed asynchronous resource.

A typical server:

```cpp
auto server = TcpServer::bind("0.0.0.0", 8080);

while (true) {
    auto connection = co_await server.accept();

    spawn(handle_connection(std::move(connection)));
}
```

Each connection can execute independently without requiring a dedicated OS thread.

---

# HTTP

HTTP is implemented above the TCP layer.

The stack is:

```text
HTTP
 │
 ▼
TCP
 │
 ▼
Socket
 │
 ▼
Channel
 │
 ▼
epoll
 │
 ▼
Linux kernel
```

The HTTP layer is responsible for:

* Request parsing
* HTTP method handling
* Headers
* Request bodies
* Response construction
* Keep-alive connections
* Status codes
* Content lengths
* Connection lifecycle

Example:

```cpp
HttpServer server(8080);

server.get("/", [](const HttpRequest& request) -> Task<HttpResponse> {
    co_return HttpResponse::ok("Hello from Crux");
});

co_await server.run();
```

---

# WebSockets

WebSockets are built on top of the asynchronous TCP infrastructure.

The lifecycle is:

```text
TCP connection
      │
      ▼
HTTP Upgrade
      │
      ▼
WebSocket handshake
      │
      ▼
Persistent connection
      │
      ├── receive frame
      ├── process message
      ├── send frame
      └── close
```

Because WebSockets are naturally long-lived connections, they are a particularly good fit for Crux's event-driven architecture.

Potential use cases include:

* Multiplayer services
* Chat systems
* Live dashboards
* Collaborative applications
* Notifications
* Market-data streams
* Real-time telemetry

---

# Request Lifecycle

A typical HTTP request travels through Crux like this:

```text
Client
  │
  │ TCP SYN
  ▼
Linux TCP Stack
  │
  ▼
Listening Socket
  │
  ▼
epoll
  │
  ▼
Accept Coroutine
  │
  ▼
TCP Connection
  │
  ▼
HTTP Parser
  │
  ▼
HTTP Request
  │
  ▼
Route
  │
  ▼
Application Handler
  │
  ▼
Business Logic
  │
  ▼
HTTP Response
  │
  ▼
Async Write
  │
  ▼
Socket
  │
  ▼
Client
```

The application handler can suspend at any point without blocking the event-loop thread.

---

# Concurrency Model

Crux follows an **event-driven concurrency model**.

Suppose 10,000 clients maintain connections:

```text
Traditional thread-per-connection

10,000 connections
       │
       ▼
10,000 threads
       │
       ▼
Large memory footprint
+ context switching
+ scheduler overhead
```

Crux:

```text
10,000 connections
       │
       ▼
10,000 lightweight runtime tasks
       │
       ▼
epoll
       │
       ▼
Small number of execution threads
```

A connection that is waiting for network data does not need to consume an actively executing thread.

---

# Project Structure

```text
crux/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── include/
│   └── crux/
│       ├── core/
│       │   ├── EventLoop.hpp
│       │   ├── Scheduler.hpp
│       │   ├── Task.hpp
│       │   └── Timer.hpp
│       │
│       ├── io/
│       │   ├── EpollPoller.hpp
│       │   ├── Channel.hpp
│       │   └── AsyncIO.hpp
│       │
│       ├── net/
│       │   ├── Socket.hpp
│       │   ├── TcpServer.hpp
│       │   ├── TcpConnection.hpp
│       │   └── Acceptor.hpp
│       │
│       ├── http/
│       │   ├── HttpServer.hpp
│       │   ├── HttpRequest.hpp
│       │   ├── HttpResponse.hpp
│       │   ├── HttpParser.hpp
│       │   └── Router.hpp
│       │
│       └── websocket/
│           ├── WebSocket.hpp
│           ├── WebSocketFrame.hpp
│           └── WebSocketConnection.hpp
│
├── src/
│   ├── core/
│   ├── io/
│   ├── net/
│   ├── http/
│   └── websocket/
│
├── examples/
│   ├── echo/
│   ├── tcp_server/
│   ├── http_server/
│   └── websocket_server/
│
├── tests/
│   ├── core/
│   ├── io/
│   ├── net/
│   ├── http/
│   └── websocket/
│
├── benchmarks/
│   ├── tcp/
│   ├── http/
│   └── websocket/
│
├── docs/
│   ├── architecture/
│   ├── internals/
│   └── examples/
│
└── scripts/
    ├── build.sh
    ├── test.sh
    └── benchmark.sh
```

---

# Requirements

Crux currently targets Linux.

### Required

* Linux
* GCC 13+ or Clang 17+
* C++20
* CMake 3.20+
* Make or Ninja

### Recommended

* Ubuntu 24.04+
* Clang
* Ninja
* AddressSanitizer
* UndefinedBehaviorSanitizer

---

# Building

Clone the repository:

```bash
git clone https://github.com/<your-username>/crux.git
cd crux
```

Create a build directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Build:

```bash
cmake --build build -j$(nproc)
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

---

# Running the Examples

## TCP Echo Server

```bash
./build/examples/tcp_echo
```

Connect:

```bash
nc localhost 8080
```

Anything sent to the server is echoed back asynchronously.

---

## HTTP Server

```bash
./build/examples/http_server
```

Then:

```bash
curl http://localhost:8080/
```

Expected response:

```text
Hello from Crux
```

---

## WebSocket Server

```bash
./build/examples/websocket_server
```

The server accepts WebSocket connections and asynchronously processes incoming messages.

---

# Example

A minimal asynchronous server:

```cpp
#include <crux/crux.hpp>

using namespace crux;

Task<> handle(Connection connection) {
    while (connection.is_open()) {
        auto request = co_await connection.read();

        if (!request) {
            co_return;
        }

        auto response = process(*request);

        co_await connection.write(response);
    }
}

Task<> application() {
    auto server = co_await TcpServer::bind("0.0.0.0", 8080);

    while (true) {
        auto connection = co_await server.accept();

        spawn(handle(std::move(connection)));
    }
}

int main() {
    Runtime runtime;

    runtime.run(application());

    return 0;
}
```

The application code does not explicitly manage:

* `epoll_wait`
* coroutine handles
* continuation queues
* readiness callbacks
* thread sleeps
* manual state machines

Those responsibilities belong to the runtime.

---

# API Overview

## Runtime

```cpp
Runtime runtime;
runtime.run(application());
```

Responsible for:

* Event loop
* Scheduler
* Timers
* Runtime lifecycle

---

## Task

```cpp
Task<int> calculate() {
    co_return 42;
}
```

A `Task<T>` represents an asynchronous computation.

Examples:

```cpp
Task<void>
Task<int>
Task<Response>
```

---

## TCP Server

```cpp
auto server = co_await TcpServer::bind("127.0.0.1", 8080);
```

---

## Accept

```cpp
auto connection = co_await server.accept();
```

---

## Read

```cpp
auto data = co_await connection.read();
```

---

## Write

```cpp
co_await connection.write(data);
```

---

## Spawn

```cpp
spawn(handle_connection(std::move(connection)));
```

Schedules a coroutine for asynchronous execution.

---

# Error Handling

Crux avoids silently swallowing asynchronous errors.

Operations return either:

* successful values
* explicit error states
* exceptions where appropriate

For example:

```cpp
try {
    auto connection = co_await server.accept();
    co_await handle(connection);
}
catch (const NetworkError& error) {
    log(error);
}
```

System-level errors such as:

```text
EAGAIN
EWOULDBLOCK
ECONNRESET
EPIPE
ETIMEDOUT
```

are translated into runtime-level error handling semantics.

---

# Memory Model

Performance-oriented asynchronous runtimes need to carefully control allocations.

Crux uses:

* RAII
* Move semantics
* Smart ownership where appropriate
* Coroutine frames
* Reusable buffers
* Explicit resource lifetimes

A major goal is to avoid unnecessary heap allocation on the hot path.

For example:

```text
Request
   │
   ▼
Buffer
   │
   ▼
Parser
   │
   ▼
Handler
   │
   ▼
Response
```

The runtime attempts to reuse resources where safe rather than allocating repeatedly for every I/O operation.

---

# Performance

Crux is designed for workloads characterized by:

* High connection counts
* Small asynchronous operations
* Long-lived connections
* I/O-heavy workloads
* Real-time communication
* HTTP APIs
* WebSocket services

The primary performance goals are:

### 1. Low scheduling overhead

Coroutine suspension/resumption should be significantly cheaper than OS thread context switching.

### 2. Efficient I/O multiplexing

`epoll` allows the runtime to efficiently wait for readiness across many file descriptors.

### 3. Minimal blocking

The event-loop thread should never perform avoidable blocking I/O.

### 4. Predictable resource usage

Connection count should not linearly translate into OS thread count.

---

# Benchmarking

Crux includes benchmark targets for:

* TCP throughput
* HTTP requests/sec
* Connection establishment
* Concurrent connections
* Echo latency
* WebSocket message throughput

Example:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCRUX_BUILD_BENCHMARKS=ON

cmake --build build -j$(nproc)
```

Run:

```bash
./build/benchmarks/http_benchmark
```

Benchmark results should always be compared using the same:

* CPU
* kernel
* compiler
* optimization level
* payload size
* connection count
* workload
* network conditions

Performance numbers without workload definitions are intentionally not treated as meaningful.

---

# Design Trade-offs

Crux intentionally makes several platform and architecture choices.

## Why Linux?

The first implementation targets Linux because `epoll` provides an efficient and mature I/O multiplexing primitive.

Cross-platform abstractions can be added later.

Potential future backends include:

```text
Linux      → epoll
macOS      → kqueue
Windows    → IOCP
```

The higher-level runtime should remain independent of the operating-system polling mechanism.

---

## Why C++20 Coroutines?

C++20 coroutines provide:

* Compiler-supported suspension
* Low-level control
* No mandatory runtime scheduler
* Strong integration with C++
* Efficient coroutine frames
* Familiar sequential syntax

They allow Crux to build its own async abstraction without depending on a third-party runtime.

---

## Why epoll?

`epoll` is particularly well suited for Linux servers with large numbers of active file descriptors.

Instead of asking:

```text
"Is socket 1 ready?"
"Is socket 2 ready?"
"Is socket 3 ready?"
...
```

the runtime asks the kernel:

```text
"Tell me which registered resources are ready."
```

This makes readiness notification scalable.

---

# Crux vs Traditional Servers

| Model                 | Concurrency              | Blocking | Scheduling          |
| --------------------- | ------------------------ | -------- | ------------------- |
| Thread per connection | OS threads               | Possible | OS scheduler        |
| Thread pool           | Limited by pool          | Possible | OS scheduler        |
| Callback event loop   | Event-driven             | Avoided  | Callback queue      |
| Crux                  | Coroutine + event-driven | Avoided  | Coroutine scheduler |

Crux combines the event-driven efficiency of callback runtimes with the programming ergonomics of structured asynchronous code.

---

# Runtime Philosophy

Crux follows several design principles.

### 1. Async should look synchronous

Application code should describe **what happens**, not manually manage continuations.

### 2. The runtime should stay small

Complexity should not be introduced unless it solves a real runtime problem.

### 3. No hidden blocking

Operations that may block should be explicit and integrated with the runtime.

### 4. Ownership must be obvious

Connections, sockets, buffers, and coroutine lifetimes should have predictable ownership.

### 5. Performance should be measurable

Every major optimization should be validated using benchmarks and profiling rather than assumptions.

### 6. Runtime internals should remain understandable

Crux is intended to be both a useful runtime and a learning-oriented systems project.

---

# Roadmap

## Phase 1 — Core Runtime

* [x] CMake build system
* [x] Linux runtime foundation
* [x] `epoll` wrapper
* [x] Event loop
* [x] Channel abstraction
* [x] File descriptor lifecycle
* [x] C++20 coroutine foundation
* [x] Task abstraction
* [x] Coroutine scheduler

## Phase 2 — Networking

* [x] Non-blocking sockets
* [x] TCP acceptor
* [x] TCP connections
* [x] Async read
* [x] Async write
* [x] Connection lifecycle
* [x] Error propagation

## Phase 3 — Protocols

* [x] HTTP parser
* [x] HTTP server
* [x] Routing
* [x] HTTP responses
* [x] Keep-alive
* [x] WebSocket handshake
* [x] WebSocket frames
* [x] WebSocket connections

## Phase 4 — Runtime Optimization

* [x] Buffer reuse
* [x] Allocation profiling
* [x] Coroutine lifecycle optimization
* [x] Event dispatch optimization
* [x] Benchmark suite
* [x] Sanitizer integration
* [x] Runtime profiling

## Phase 5 — Production Hardening

* [x] Graceful shutdown
* [x] Connection limits
* [x] Timeout handling
* [x] Backpressure
* [x] Robust error handling
* [x] Stress testing
* [x] Fuzz testing
* [x] Documentation

## Future

* [ ] Multi-threaded event loops
* [ ] CPU affinity
* [ ] Work-stealing scheduler
* [ ] Linux `io_uring` backend
* [ ] TLS
* [ ] HTTP/2
* [ ] HTTP/3
* [ ] macOS `kqueue` backend
* [ ] Windows IOCP backend
* [ ] Production-grade observability
* [ ] Pluggable memory allocators

---

# Multi-Threaded Architecture

The initial Crux architecture intentionally uses a single event-loop execution model.

Future versions can scale across cores:

```text
                    Crux Runtime
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
      EventLoop 0    EventLoop 1    EventLoop 2
          │              │              │
        epoll          epoll          epoll
          │              │              │
       CPU 0           CPU 1           CPU 2
```

This allows the runtime to preserve the event-driven model while taking advantage of multiple CPU cores.

---

# io_uring

A future Linux backend may support `io_uring`.

The architecture would then become:

```text
                 Crux I/O abstraction
                         │
              ┌──────────┴──────────┐
              │                     │
              ▼                     ▼
            epoll                io_uring
              │                     │
              └──────────┬──────────┘
                         │
                         ▼
                    Event Loop
```

The application-facing API should remain unchanged.

This is an important architectural boundary:

> **Application code should not need to know which kernel I/O mechanism is being used.**

---

# Testing

Crux uses multiple layers of testing.

## Unit Tests

Test individual components:

```text
EventLoop
Channel
Scheduler
Task
Socket
Parser
Router
```

## Integration Tests

Test interactions:

```text
EventLoop
   +
Socket
   +
Coroutine
   +
TCP
```

## End-to-End Tests

Example:

```text
Client
  │
  ▼
TCP
  │
  ▼
HTTP
  │
  ▼
Router
  │
  ▼
Handler
  │
  ▼
Response
```

## Stress Tests

The runtime is tested under:

* Large connection counts
* High request rates
* Slow clients
* Fast clients
* Repeated connect/disconnect cycles
* Partial reads
* Partial writes
* Connection resets

---

# Debugging

Crux is designed to work with standard Linux debugging tools.

## AddressSanitizer

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCRUX_ENABLE_ASAN=ON

cmake --build build
```

## UndefinedBehaviorSanitizer

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCRUX_ENABLE_UBSAN=ON
```

## GDB

```bash
gdb ./build/examples/http_server
```

## System Call Tracing

```bash
strace ./build/examples/http_server
```

This is particularly useful for inspecting:

```text
socket()
bind()
listen()
accept4()
epoll_ctl()
epoll_wait()
read()
write()
close()
```

---

# Profiling

For CPU profiling:

```bash
perf record -g ./build/benchmarks/http_benchmark
```

Then:

```bash
perf report
```

Useful profiling targets include:

* Event dispatch
* Coroutine scheduling
* Coroutine resume/suspend
* Socket operations
* HTTP parsing
* Memory allocation
* Timer processing

The goal is to identify actual hot paths rather than optimize based on intuition.

---

# Development

Clone the repository:

```bash
git clone https://github.com/<your-username>/crux.git
cd crux
```

Configure a debug build:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug
```

Build:

```bash
cmake --build build -j$(nproc)
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Run sanitizers:

```bash
./scripts/test.sh --sanitizers
```

Run benchmarks:

```bash
./scripts/benchmark.sh
```

---

# Contributing

Contributions are welcome.

Before submitting a pull request:

1. Build the project successfully.
2. Run the complete test suite.
3. Run relevant sanitizers.
4. Add tests for new functionality.
5. Benchmark performance-sensitive changes.
6. Document public API changes.

For architectural changes, open an issue first and explain:

* The problem
* Proposed design
* Alternatives considered
* Performance implications
* API implications

---

# Design Guidelines for Contributors

When adding a new feature, prefer:

```text
Small abstraction
       │
       ▼
Explicit ownership
       │
       ▼
Composable async operation
       │
       ▼
Testable implementation
```

Avoid:

* Hidden blocking operations
* Global mutable state
* Unnecessary allocations
* Tight coupling between protocol and runtime layers
* OS-specific logic leaking into high-level APIs

---

# Security Considerations

Crux is a systems-level networking runtime and should not be considered production-safe solely because the runtime successfully handles normal workloads.

Production deployments should consider:

* Connection limits
* Request-size limits
* Header-size limits
* Read/write timeouts
* Idle connection timeouts
* Backpressure
* TLS
* Input validation
* Resource exhaustion
* Slow-client attacks
* Malformed protocol input

Protocol parsers should treat all network input as untrusted.

---

# What Makes Crux Interesting?

Crux is intentionally more than a networking library.

It is an exploration of how modern asynchronous runtimes work internally.

The project brings together:

```text
Linux kernel
     │
     ▼
epoll
     │
     ▼
Event-driven I/O
     │
     ▼
Coroutine suspension
     │
     ▼
Coroutine scheduling
     │
     ▼
TCP networking
     │
     ▼
HTTP / WebSockets
     │
     ▼
Application runtime
```

Instead of treating asynchronous execution as a black box, Crux exposes and implements the fundamental mechanisms behind it.

That makes it useful both as a high-performance runtime project and as a deep systems-programming exercise.

---

# Architecture at a Glance

```text
                         ┌─────────────────────┐
                         │     Application     │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │ HTTP / WebSocket   │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │    TCP Layer       │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │ Async I/O Awaitable │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │ Coroutine Scheduler │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │     Event Loop      │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │       epoll         │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │    Linux Kernel     │
                         └─────────────────────┘
```

---

# Philosophy

Crux is built around a simple principle:

> **High-performance networking should not require application developers to manually manage asynchronous state machines.**

The runtime handles the machinery.

The developer writes the behavior.

```cpp
auto request = co_await connection.read();

auto response = process(request);

co_await connection.write(response);
```

Underneath those few lines are:

```text
non-blocking sockets
        +
epoll
        +
event dispatch
        +
coroutine suspension
        +
coroutine resumption
        +
scheduler
        +
buffer management
        +
connection lifecycle
```

That is the core of Crux.

## ⭐ If You Find Crux Interesting

If you're interested in:

* C++
* Linux internals
* Event-driven systems
* Async runtimes
* C++20 coroutines
* Network programming
* Distributed systems
* High-performance backend engineering

feel free to explore the implementation, open issues, or contribute.

**Crux — understand the runtime, don't just use it.**
