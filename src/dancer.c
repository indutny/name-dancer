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


static void dancer_client_proxy_close_cb(uv_proxy_t* p) {
  dancer_client_t* c;

  c = p->data;
  free(c);
}


static void dancer_client_close(dancer_client_t* client, int err) {
  LOG("close err=%d", err);
  if (client->init == kDancerClientInitialized)
    return uv_proxy_close(&client->proxy, dancer_client_proxy_close_cb);

  /* TODO(indutny): implement me */
  abort();
}


static void dancer_client_proxy_error(uv_proxy_t* proxy, uv_link_t* side,
                                      int err) {
  dancer_client_t* c;

  c = proxy->data;

  LOG("proxy err=%d", err);
  dancer_client_close(c, err);
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


static int dancer_pton(struct sockaddr_storage* addr, const char* ip,
                       size_t ip_len, int port) {
  char tmp[1024];
  struct sockaddr_in* addr4;
  struct sockaddr_in6* addr6;
  int err;

  memset(addr, 0, sizeof(*addr));
  addr4 = (struct sockaddr_in*) addr;
  addr6 = (struct sockaddr_in6*) addr;

  /* TODO(indutny): check return value */
  snprintf(tmp, sizeof(tmp), "%.*s", (int) ip_len, ip);

  /* Try both IPv4 and IPv6 parsers */
  err = uv_inet_pton(AF_INET, tmp, &addr4->sin_addr);
  if (err == 0) {
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(port);
  } else {
    err = uv_inet_pton(AF_INET6, tmp, &addr6->sin6_addr);
    if (err != 0)
      return err;

    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(port);
  }

  return 0;
}


static void dancer_client_connect_cb(uv_connect_t* req, int status) {
  dancer_client_t* client;
  int err;

  client = req->data;

  if (status == UV_ECANCELED)
    return;
  if (status != 0)
    return dancer_client_close(client, status);

  LOG("connected to upstream");

  err = dancer_parser_stream(&client->parser);
  if (err != 0)
    return dancer_client_close(client, err);

  err = uv_link_read_start((uv_link_t*) &client->proxy.right);
  if (err != 0)
    return dancer_client_close(client, err);
}


static void dancer_client_parser_cb(dancer_parser_t* p, const char* name,
                                    unsigned int name_len) {
  dancer_client_t* client;
  const char* ip;
  struct sockaddr_storage addr;
  int err;

  client = p->data;
  LOG("SNI=%.*s", name_len, name);

  if (name_len == 14 && strncmp("www.google.com", name, name_len) == 0)
    ip = "172.217.10.36";
  else if (name_len == 16 && strncmp("www.facebook.com", name, name_len) == 0)
    ip = "31.13.69.228";
  else
    return dancer_client_close(client, UV_EINVAL);

  err = dancer_pton(&addr, ip, strlen(ip), 443);
  if (err != 0)
    return dancer_client_close(client, err);

  err = uv_tcp_connect(&client->connect, &client->upstream.tcp,
                       (struct sockaddr*) &addr, dancer_client_connect_cb);
  if (err != 0)
    return dancer_client_close(client, err);
  client->connect.data = client;
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
  client->parser.data = client;

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

  st->loop = options->loop;

  err = dancer_pton(&addr, options->host, strlen(options->host), options->port);
  if (err != 0)
    return err;

  st->server.data = st;

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
