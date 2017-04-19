#ifndef SRC_PARSER_H_
#define SRC_PARSER_H_

#include "uv_link_t.h"

#define DANCER_PARSER_MAX_HELLO 16384

typedef struct dancer_parser_s dancer_parser_t;
typedef void (*dancer_parser_cb)(dancer_parser_t* parser, const char* name,
                                 unsigned int name_len);

enum dancer_parser_state_e {
  kDancerParserStateBuffer,
  kDancerParserStateStream
};
typedef enum dancer_parser_state_e dancer_parser_state_t;

struct dancer_parser_s {
  uv_link_t link;

  /* Private */
  dancer_parser_state_t state;
  dancer_parser_cb cb;

  /* TODO(indutny): allocate/deallocate this */
  unsigned char buffer[DANCER_PARSER_MAX_HELLO];
  unsigned int off;
};

int dancer_parser_init(dancer_parser_t* parser, dancer_parser_cb cb);
int dancer_parser_stream(dancer_parser_t* parser);

#endif  /* SRC_PARSER_H_ */
