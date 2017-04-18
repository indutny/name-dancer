#ifndef SRC_PARSER_H_
#define SRC_PARSER_H_

#include "uv_link_t.h"
#include "ringbuffer.h"

typedef struct dancer_parser_s dancer_parser_t;
typedef void (*dancer_parser_cb)(dancer_parser_t* parser, const char* name);

struct dancer_parser_s {
  uv_link_t link;

  /* Private */
  dancer_parser_cb cb;
  ringbuffer buffer;
};

int dancer_parser_init(dancer_parser_t* parser, dancer_parser_cb cb);

#endif  /* SRC_PARSER_H_ */
