#include <stdint.h>

#include "src/parser.h"

typedef struct ssl_version_s ssl_version_t;

struct ssl_version_s {
  uint8_t major;
  uint8_t minor;
};

static const uint8_t kRecordTypeHandshake = 22;
static const uint8_t kHandshakeTypeClientHello = 1;
static const uint8_t kExtensionTypeServername = 0;


static void dancer_parser_close(uv_link_t* link, uv_link_t* source,
                                uv_link_close_cb cb) {
  dancer_parser_t* p;

  p = link->data;
  cb(source);
}


#define ENSURE(N)                                                             \
    do {                                                                      \
      if ((N) > max_len) goto invalid;                                        \
    } while (0)


#define SKIP(N)                                                               \
    do {                                                                      \
      buf += (N);                                                             \
      max_len -= (N);                                                         \
    } while (0)


static int dancer_parser_run_sni(dancer_parser_t* parser,
                                 unsigned char* buf,
                                 unsigned int max_len) {
  uint8_t name_type;
  uint16_t name_len;
  uint16_t len;
  dancer_parser_cb cb;
  int err;

  ENSURE(2);
  len = (buf[0] << 8) | buf[1];
  SKIP(2);

  ENSURE(len);

  ENSURE(3);
  name_type = buf[0];
  name_len = (buf[1] << 8) | buf[2];
  SKIP(3);

  ENSURE(name_len);

  err = uv_link_read_stop(&parser->link);
  if (err != 0)
    return err;

  cb = parser->cb;
  parser->cb = NULL;

  cb(parser, (const char*) buf, name_len);
  return 0;

invalid:
  return UV_EINVAL;
}


static int dancer_parser_run(dancer_parser_t* parser) {
  unsigned int max_len;
  unsigned int hard_max;
  unsigned char* buf;
  ssl_version_t version;
  unsigned int len;
  unsigned int off;

  if (parser->cb == NULL)
    return 0;

  max_len = parser->off;
  hard_max = sizeof(parser->buffer);
  buf = parser->buffer;
  if (max_len < 9)
    goto not_ready;

  if (buf[0] != kRecordTypeHandshake)
    goto invalid;

  version.major = buf[1];
  version.minor = buf[2];

  /* TLSv1.0, TLSv1.1, TLSv1.2 */
  if (version.major != 3 || version.minor < 1 || version.minor > 3)
    goto invalid;

  len = (buf[3] << 8) | buf[4];
  if (len > hard_max - 5)
    goto invalid;
  if (len > max_len - 5)
    goto not_ready;

  /* Skip record header */
  buf += 5;
  hard_max -= 5;
  max_len = len;

  if (buf[0] != kHandshakeTypeClientHello)
    goto invalid;

  len = (buf[1] << 16) | (buf[2] << 8) | buf[3];
  if (len > hard_max - 4)
    goto invalid;
  if (len > max_len - 4)
    goto not_ready;

  buf += 4;
  max_len = len;

  /* Skip version(2), random(32) */
  ENSURE(2 + 32);
  SKIP(2 + 32);

  /* Skip session */
  ENSURE(1);
  len = buf[0];
  SKIP(1);
  if (len > 32)
    goto invalid;

  ENSURE(len);
  SKIP(len);

  /* Skip ciphers */
  ENSURE(2);
  len = (buf[0] << 16) | buf[1];
  SKIP(2);

  ENSURE(len);
  SKIP(len);

  /* Skip compression */
  ENSURE(1);
  len = buf[0];
  SKIP(1);

  ENSURE(len);
  SKIP(len);

  /* Parse extensions */
  ENSURE(2);
  len = (buf[0] << 16) | buf[1];
  SKIP(2);

  ENSURE(len);
  max_len = len;
  for (off = 0; off < len; ) {
    uint16_t ext_type;
    unsigned int ext_len;
    unsigned char* ext_body;

    ENSURE(4);
    ext_type = (buf[0] << 16) | buf[1];
    ext_len = (buf[2] << 16) | buf[3];
    SKIP(4);
    ENSURE(ext_len);

    ext_body = buf;
    SKIP(ext_len);
    if (ext_type != kExtensionTypeServername)
      continue;

    return dancer_parser_run_sni(parser, ext_body, ext_len);
  }

  return UV_ENOTSUP;

not_ready:
  return UV_EAGAIN;

invalid:
  return UV_EINVAL;
}


#undef SKIP


void dancer_parser_alloc_cb(uv_link_t* link,
                            size_t suggested_size,
                            uv_buf_t* buf) {
  dancer_parser_t* p;

  p = link->data;
  if (p->state == kDancerParserStateStream)
    return uv_link_propagate_alloc_cb(link, suggested_size, buf);

  *buf = uv_buf_init((char*) p->buffer + p->off, sizeof(p->buffer) - p->off);
}


void dancer_parser_read_cb(uv_link_t* link,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  dancer_parser_t* p;
  uv_buf_t tmp;
  int err;

  p = link->data;
  if (p->state == kDancerParserStateStream)
    return uv_link_propagate_read_cb(link, nread, buf);

  /* Propagate errors */
  if (nread < 0) {
    err = nread;
    goto fail;
  }

  p->off += nread;

  if (p->off == sizeof(p->buffer)) {
    err = uv_link_read_stop(link->parent);
    if (err != 0)
      goto fail;
  }

  err = dancer_parser_run(p);
  if (err != 0 && err != UV_EAGAIN)
    goto fail;

  return;

fail:
  uv_link_propagate_alloc_cb(link, 1, &tmp);
  uv_link_propagate_read_cb(link, err, &tmp);
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
  parser->off = 0;
  return 0;
}


int dancer_parser_stream(dancer_parser_t* parser) {
  unsigned int off;
  parser->state = kDancerParserStateStream;

  /* Feed all accumulated data */
  for (off = 0; off < parser->off; ) {
    uv_buf_t buf;
    unsigned int to_copy;

    uv_link_propagate_alloc_cb(&parser->link, parser->off - off, &buf);
    if (buf.base == NULL || buf.len == 0) {
      uv_link_propagate_read_cb(&parser->link, UV_ENOBUFS, &buf);
      return UV_ENOBUFS;
    }

    to_copy = buf.len;
    if (to_copy > parser->off - off)
      to_copy = parser->off - off;
    memcpy(buf.base, parser->buffer + off, to_copy);

    /* TODO(indutny): this may break down if down-link will read_stop us */
    uv_link_propagate_read_cb(&parser->link, to_copy, &buf);

    off += to_copy;
  }

  if (parser->off != sizeof(parser->buffer) && parser->cb != NULL)
    return 0;

  return uv_link_read_start(parser->link.parent);
}
