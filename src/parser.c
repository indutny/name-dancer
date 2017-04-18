#include "src/parser.h"

static void dancer_parser_close(uv_link_t* link, uv_link_t* source,
                                uv_link_close_cb cb) {
  dancer_parser_t* p;

  p = link->data;
  ringbuffer_destroy(&p->buffer);
  cb(source);
}


void dancer_parser_alloc_cb(uv_link_t* link,
                            size_t suggested_size,
                            uv_buf_t* buf) {
  dancer_parser_t* p;
  size_t avail;
  char* data;

  p = link->data;
  if (p->state == kDancerParserStateStream)
    return uv_link_propagate_alloc_cb(link, suggested_size, buf);

  avail = suggested_size;
  data = ringbuffer_write_ptr(&p->buffer, &avail);

  *buf = uv_buf_init(data, avail);
}


void dancer_parser_read_cb(uv_link_t* link,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  dancer_parser_t* p;

  p = link->data;
  if (p->state == kDancerParserStateStream)
    return uv_link_propagate_read_cb(link, nread, buf);

  /* Propagate errors */
  if (nread <= 0) {
    uv_buf_t tmp;
    uv_link_propagate_alloc_cb(link, 1, &tmp);
    return uv_link_propagate_read_cb(link, nread, &tmp);
  }

  ringbuffer_write_append(&p->buffer, nread);
}


static uv_link_methods_t dancer_parser_methods = {
  .read_start = uv_link_default_read_start,
  .read_stop = uv_link_default_read_stop,
  .write = uv_link_default_write,
  .try_write = uv_link_default_try_write,
  .shutdown = uv_link_default_shutdown,
  .close = dancer_parser_close,

  .alloc_cb_override = dancer_parser_alloc_cb,
  .read_cb_override = dancer_parser_read_cb
};


int dancer_parser_init(dancer_parser_t* parser, dancer_parser_cb cb) {
  int err;

  err = uv_link_init(&parser->link, &dancer_parser_methods);
  if (err != 0)
    return err;

  parser->state = kDancerParserStateBuffer;
  parser->link.data = parser;

  parser->cb = cb;
  ringbuffer_init(&parser->buffer);
  return 0;
}
