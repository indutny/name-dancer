#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "src/dancer.h"

static const int kDancerBacklog = 127;


#define LOG(...)                                                              \
  do {                                                                        \
    fprintf(stderr, "LOG: " __VA_ARGS__);                                     \
    fprintf(stderr, "\n");                                                    \
  } while (0)


static void dancer_client_proxy_error(uv_proxy_t* proxy, uv_link_t* side,
                                      int err) {
  dancer_client_t* c;

  c = proxy->data;
}


static void dancer_client_close(dancer_client_t* client, int err) {
  /* TODO(indutny): implement me */
  abort();
}


static int dancer_client_init_side(dancer_client_t* client,
                                   dancer_side_t* side) {
  int err;

  err = uv_tcp_init(client->state->loop, &side->tcp);
  if (err != 0)
    return err;

  side->init = kDancerSideInitTCP;

  err = uv_link_source_init(&side->source, (uv_stream_t*) &side->tcp);
  if (err != 0)
    return err;

  side->init = kDancerSideInitSource;

  side->source.data = client;
  return 0;
}


static void dancer_client_parser_cb(dancer_parser_t* p, const char* name) {
}


static void dancer_conn_cb(uv_stream_t* server, int status) {
  dancer_t* st;
  dancer_client_t* client;
  int err;

  st = server->data;

  client = calloc(1, sizeof(*client));
  if (client == NULL) {
    LOG("failed to allocate client");
    return;
  }

  client->state = st;

  err = dancer_client_init_side(client, &client->incoming);
  if (err != 0)
    goto fail;

  err = dancer_client_init_side(client, &client->upstream);
  if (err != 0)
    goto fail;

  err = dancer_parser_init(&client->parser, dancer_client_parser_cb);
  if (err != 0)
    goto fail;
  client->init = kDancerClientInitBuffer;

  err = uv_proxy_init(&client->proxy, dancer_client_proxy_error);
  if (err != 0)
    goto fail;

  client->init = kDancerClientInitProxy;
  client->proxy.data = client;

  /* Incoming -> Parser */
  err = uv_link_chain((uv_link_t*) &client->incoming.source,
                      &client->parser.link);
  if (err != 0)
    goto fail;
  client->init = kDancerClientInitLinkIncoming;

  /* Parser -> Proxy.left */
  err = uv_link_chain(&client->parser.link, &client->proxy.left);
  if (err != 0)
    goto fail;
  client->init = kDancerClientInitLinkParser;

  /* Upstream -> Proxy.right */
  err = uv_link_chain((uv_link_t*) &client->upstream.source,
                      &client->proxy.right);
  if (err != 0)
    goto fail;
  client->init = kDancerClientInitLinkUpstream;

  err = uv_accept(server, (uv_stream_t*) &client->incoming.tcp);
  if (err != 0)
    goto fail;

  err = uv_link_read_start((uv_link_t*) &client->proxy.left);
  if (err != 0)
    goto fail;

  return;

fail:
  dancer_client_close(client, err);
}


int dancer_init(dancer_t* st, dancer_options_t* options) {
  int err;
  struct sockaddr_storage addr;
  struct sockaddr_in* addr4;
  struct sockaddr_in6* addr6;

  addr4 = (struct sockaddr_in*) &addr;
  addr6 = (struct sockaddr_in6*) &addr;

  st->loop = options->loop;

  st->server.data = st;

  memset(&addr, 0, sizeof(addr));

  /* Try both IPv4 and IPv6 parsers */
  err = uv_inet_pton(AF_INET, options->host, &addr4->sin_addr);
  if (err == 0) {
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(options->port);
  } else {
    err = uv_inet_pton(AF_INET6, options->host, &addr6->sin6_addr);
    if (err != 0)
      return err;

    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(options->port);
  }

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


void dancer_destroy(dancer_t* st) {
  uv_close((uv_handle_t*) &st->server, NULL);
}
