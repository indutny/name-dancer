#include <stdio.h>

#include "src/dancer.h"

#include "uv.h"

int main() {
  int err;
  dancer_t st;
  dancer_options_t opts;

  opts.loop = uv_default_loop();
  opts.host = "::";
  opts.port = 1443;

  err = dancer_init(&st, &opts);
  if (err != 0) {
    fprintf(stderr, "Failed to initialize dancer state: [%d] %s\n", err,
            uv_strerror(err));
    return -1;
  }

  uv_run(opts.loop, UV_RUN_DEFAULT);

  dancer_destroy(&st);

  return 0;
}
