#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "src/dancer.h"

static const int kDancerBacklog = 127;


static void dancer_client_close_cb(uv_handle_t* handle) {
  dancer_client_t* client;

  client = handle->data;
  handle->data = NULL;

  /* Still closing */
  if (client->incoming.tcp.data != NULL || client->upstream.tcp.data != NULL)
    return;

  ringbuffer_destroy(&client->buffer);
  free(client);
}


static void dancer_client_close(dancer_client_t* client) {
  if (client->incoming.tcp.data != NULL)
    uv_close((uv_handle_t*) &client->incoming.tcp, dancer_client_close_cb);
  if (client->upstream.tcp.data != NULL)
    uv_close((uv_handle_t*) &client->upstream.tcp, dancer_client_close_cb);
}


static void dancer_client_shutdown(dancer_client_t* client) {
}


static void dancer_client_alloc_cb(uv_handle_t* handle,
                                   size_t suggested_size,
                                   uv_buf_t* buf) {
  dancer_client_t* client;
  size_t avail;
  char* ptr;

  client = handle->data;

  avail = 0;
  ptr = ringbuffer_write_ptr(&client->buffer, &avail);
  *buf = uv_buf_init(ptr, avail);
}


static void dancer_client_read_cb(uv_stream_t* stream,
                                  ssize_t nread,
                                  const uv_buf_t* buf) {
  dancer_client_t* client;
  int err;

  client = stream->data;
  if (nread >= 0)
    err = ringbuffer_write_append(&client->buffer, nread);
  else
    err = 0;

  if (err != 0)
    return dancer_client_close(client);

  if (nread == UV_EOF)
    dancer_client_shutdown(client);
}


static void dancer_conn_cb(uv_stream_t* server, int status) {
  int err;
  dancer_t* st;
  dancer_client_t* client;

  st = server->data;

  client = malloc(sizeof(*client));
  if (client == NULL)
    goto fail;

  client->st = st;
  ringbuffer_init(&client->buffer);

  client->incoming.tcp.data = NULL;
  client->upstream.tcp.data = NULL;

  err = uv_tcp_init(st->loop, &client->incoming.tcp);
  if (err != 0)
    goto fail;

  client->incoming.tcp.data = client;

  err = uv_tcp_init(st->loop, &client->upstream.tcp);
  if (err != 0)
    goto fail_async;

  client->upstream.tcp.data = client;

  err = uv_accept((uv_stream_t*) server, (uv_stream_t*) &client->incoming.tcp);
  if (err != 0)
    goto fail_async;

  uv_read_start((uv_stream_t*) &client->incoming.tcp, dancer_client_alloc_cb,
                dancer_client_read_cb);

  return;

fail_async:
  dancer_client_close(client);
  client = NULL;

fail:
  free(client);

  /* TODO(indutny): log */
  fprintf(stderr, "failed to accept\n");
}


int dancer_init(dancer_t* st, dancer_options_t* options) {
  int err;
  struct sockaddr_in addr;

  st->loop = options->loop;

  st->server.data = st;

  /* TODO(indutny): ipv6 */
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options->port);

  err = uv_inet_pton(AF_INET, options->host, &addr.sin_addr);
  if (err != 0)
    return err;

  err = uv_tcp_init(st->loop, &st->server);
  if (err != 0)
    return err;

  err = uv_tcp_bind(&st->server, (struct sockaddr*) &addr, 0);
  if (err != 0)
    goto fail_bind;

  err = uv_listen((uv_stream_t*) &st->server, kDancerBacklog, dancer_conn_cb);
  if (err != 0)
    goto fail_bind;

  return 0;

fail_bind:
  uv_close((uv_handle_t*) &st->server, NULL);
  return err;
}


void dancer_run(dancer_t* st) {
  uv_run(st->loop, UV_RUN_DEFAULT);
}


void dancer_destroy(dancer_t* st) {
  uv_close((uv_handle_t*) &st->server, NULL);
}
