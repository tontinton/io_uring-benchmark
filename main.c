#define _GNU_SOURCE
#include <ctype.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_PORT 3003
#define QUEUE_DEPTH 128
#define READ_SZ 1024
#define BACKLOG 1024
#define BUFFER_ENTRIES BACKLOG
#define THREADS 10

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_READ 1
#define EVENT_TYPE_WRITE 2
#define EVENT_TYPE_CLOSE 3

#define ARRAY_SIZE(arr) sizeof(arr) / sizeof(arr[0])

struct request {
  int event_type;
  int client_socket;
};

char bufs[BUFFER_ENTRIES][READ_SZ];

const char content[] = "HTTP/1.1 200 OK\r\ncontent-length: 12\r\nconnection: "
                       "keep-alive\r\n\r\nHello world!";
const size_t content_len = ARRAY_SIZE(content) - 1;

void fatal_error(const char *msg) {
  perror(msg);
  exit(1);
}

void *zh_malloc(size_t size) {
  void *buf = malloc(size);
  if (!buf)
    fatal_error("malloc()");
  return buf;
}

struct io_uring_buf_ring *setup_buffer_ring(struct io_uring *ring) {
  struct io_uring_buf_reg reg = {0};
  struct io_uring_buf_ring *br;

  if (posix_memalign((void **)&br, sysconf(_SC_PAGESIZE),
                     BUFFER_ENTRIES * sizeof(struct io_uring_buf_ring)))
    return NULL;

  reg.ring_addr = (unsigned long)br;
  reg.ring_entries = BUFFER_ENTRIES;
  reg.bgid = 0;
  if (io_uring_register_buf_ring(ring, &reg, 0))
    return NULL;

  io_uring_buf_ring_init(br);
  for (int i = 0; i < BUFFER_ENTRIES; i++) {
    io_uring_buf_ring_add(br, bufs[i], READ_SZ, i,
                          io_uring_buf_ring_mask(BUFFER_ENTRIES), i);
  }

  io_uring_buf_ring_advance(br, BUFFER_ENTRIES);
  return br;
}

int setup_listening_socket(int port) {
  struct sockaddr_in srv_addr;

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    fatal_error("socket()");

  int enable = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    fatal_error("setsockopt(SO_REUSEADDR)");

  enable = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
    fatal_error("setsockopt(SO_REUSEPORT)");

  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(sock, (const struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0)
    fatal_error("bind()");

  if (listen(sock, BACKLOG) < 0)
    fatal_error("listen()");

  return sock;
}

bool add_accept_request(struct io_uring *ring, int server_socket,
                        struct sockaddr_in *client_addr,
                        socklen_t *client_addr_len) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe)
    return false;
  io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr,
                       client_addr_len, 0);
  struct request *req = zh_malloc(sizeof(*req));
  req->event_type = EVENT_TYPE_ACCEPT;
  io_uring_sqe_set_data(sqe, req);
  return true;
}

bool add_read_request(struct io_uring *ring, struct request *req,
                      int client_socket) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe)
    return false;
  req->event_type = EVENT_TYPE_READ;
  req->client_socket = client_socket;
  io_uring_prep_read(sqe, client_socket, NULL, READ_SZ, 0);
  io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
  io_uring_sqe_set_data(sqe, req);
  sqe->buf_group = 0;
  return true;
}

bool add_write_request(struct io_uring *ring, struct request *req) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe)
    return false;
  req->event_type = EVENT_TYPE_WRITE;
  io_uring_prep_write(sqe, req->client_socket, content, content_len, 0);
  io_uring_sqe_set_data(sqe, req);
  return true;
}

bool add_close_request(struct io_uring *ring, struct request *req) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (!sqe)
    return false;
  req->event_type = EVENT_TYPE_CLOSE;
  io_uring_prep_close(sqe, req->client_socket);
  io_uring_sqe_set_data(sqe, req);
  return true;
}

void server_loop(struct io_uring *ring, int server_socket) {
  struct io_uring_cqe *cqes[BACKLOG];
  struct sockaddr_in client_addr;
  int buffer_id;
  socklen_t client_addr_len = sizeof(client_addr);

  struct io_uring_buf_ring *br = setup_buffer_ring(ring);
  if (!br)
    fatal_error("setup_buffer_ring()");

  add_accept_request(ring, server_socket, &client_addr, &client_addr_len);

  while (true) {
    if (io_uring_submit_and_wait(ring, 1) < 0)
      fatal_error("io_uring_submit_and_wait()");

    const int count = io_uring_peek_batch_cqe(ring, cqes, ARRAY_SIZE(cqes));
    for (int i = 0; i < count; ++i) {
      struct io_uring_cqe *cqe = cqes[i];
      struct request *req = (struct request *)cqe->user_data;
      const int res = cqe->res;

      if (res < 0) {
        if (res == -ECONNRESET) {
          io_uring_cqe_seen(ring, cqe);
          continue;
        }

        fprintf(stderr, "Async request failed: %s for event: %d\n",
                strerror(-res), req->event_type);
        exit(1);
      }

      switch (req->event_type) {
      case EVENT_TYPE_ACCEPT:
        if (!add_accept_request(ring, server_socket, &client_addr,
                                &client_addr_len))
          continue;

        const int yes = 1;
        if (-1 == setsockopt(res, IPPROTO_TCP, TCP_NODELAY, (char *)&yes,
                             sizeof(int)))
          fatal_error("setsockopt(TCP_NODELAY)");

        if (!add_read_request(ring, req, res))
          continue;

        break;
      case EVENT_TYPE_READ:
        if (!res) {
          if (!add_close_request(ring, req))
            continue;
        } else if (!add_write_request(ring, req))
          continue;

        buffer_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
        io_uring_buf_ring_add(br, bufs[buffer_id], READ_SZ, buffer_id,
                              io_uring_buf_ring_mask(BUFFER_ENTRIES), 0);
        io_uring_buf_ring_advance(br, 1);
        break;
      case EVENT_TYPE_WRITE:
        if (!add_read_request(ring, req, req->client_socket))
          continue;
        break;
      case EVENT_TYPE_CLOSE:
        free(req);
        break;
      }

      io_uring_cqe_seen(ring, cqe);
    }
  }
}

int bind_thread_to_core(long core_id) {
  cpu_set_t cpuset;

  if (core_id < 0 || core_id >= sysconf(_SC_NPROCESSORS_ONLN))
    return -1;

  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

void *thread_start(void *arg) {
  struct io_uring ring;

  if (0 != bind_thread_to_core((long)arg))
    fatal_error("bind_thread_to_core()");

  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0)
    fatal_error("io_uring_queue_init()");

  int server_socket = setup_listening_socket(SERVER_PORT);
  server_loop(&ring, server_socket);

  return NULL;
}

void sigint_handler(int signo) {
  printf("Shutting down.\n");
  exit(0);
}

int main() {
  pthread_attr_t attr;
  pthread_t threads[THREADS];

  signal(SIGINT, sigint_handler);

  if (0 != pthread_attr_init(&attr))
    fatal_error("pthread_attr_init()");

  for (int i = 0; i < THREADS; ++i) {
    if (0 != pthread_create(&threads[i], &attr, &thread_start, (void *)(long)i))
      fatal_error("pthread_create()");
  }

  pthread_attr_destroy(&attr);

  for (int i = 0; i < THREADS; ++i) {
    if (0 != pthread_join(threads[i], NULL))
      fatal_error("pthread_join()");
  }

  return 0;
}
