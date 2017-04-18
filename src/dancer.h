#ifndef SRC_DANCER_H_
#define SRC_DANCER_H_

#include "uv.h"
#include "uv_proxy_t.h"

#include "src/parser.h"

typedef struct dancer_s dancer_t;
typedef struct dancer_options_s dancer_options_t;
typedef struct dancer_client_s dancer_client_t;
typedef struct dancer_side_s dancer_side_t;

enum dancer_side_init_e {
  kDancerSideInitNone = 0,
  kDancerSideInitTCP,
  kDancerSideInitSource
};
typedef enum dancer_side_init_e dancer_side_init_t;

enum dancer_client_init_e {
  kDancerClientInitNone = 0,
  kDancerClientInitBuffer,
  kDancerClientInitProxy,
  kDancerClientInitLinkIncoming,
  kDancerClientInitLinkParser,
  kDancerClientInitLinkUpstream,

  kDancerClientInitialized = kDancerClientInitLinkUpstream
};
typedef enum dancer_client_init_e dancer_client_init_t;

struct dancer_s {
  uv_loop_t* loop;
  uv_tcp_t server;
};

struct dancer_options_s {
  uv_loop_t* loop;
  const char* host;
  short port;
};

struct dancer_side_s {
  uv_tcp_t tcp;
  uv_link_source_t source;
  dancer_side_init_t init;
};

struct dancer_client_s {
  dancer_t* state;

  uv_proxy_t proxy;
  dancer_side_t incoming;
  dancer_side_t upstream;
  dancer_parser_t parser;

  dancer_client_init_t init;
};

int dancer_init(dancer_t* st, dancer_options_t* options);
void dancer_destroy(dancer_t* st);

#endif  /* SRC_DANCER_H_ */
