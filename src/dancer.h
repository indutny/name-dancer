#ifndef SRC_DANCER_H_
#define SRC_DANCER_H_

#include "uv.h"
#include "uv_link_t.h"
#include "ringbuffer.h"

typedef struct dancer_s dancer_t;
typedef struct dancer_options_s dancer_options_t;
typedef struct dancer_client_s dancer_client_t;
typedef struct dancer_side_s dancer_side_t;

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
  uv_link_source_t source;
  uv_tcp_t tcp;
};

struct dancer_client_s {
  dancer_t* st;

  dancer_side_t incoming;
  dancer_side_t upstream;

  ringbuffer buffer;
};

int dancer_init(dancer_t* st, dancer_options_t* options);
void dancer_run(dancer_t* st);
void dancer_destroy(dancer_t* st);

#endif  /* SRC_DANCER_H_ */
