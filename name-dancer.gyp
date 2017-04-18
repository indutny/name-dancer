{
  "variables": {
    "gypkg_deps": [
      "git://github.com/libuv/libuv.git@^1.9.0 => uv.gyp:libuv",
      "git://github.com/gypkg/ringbuffer@^1.0.0 [gpg] => ringbuffer.gyp:ringbuffer",
      "git://github.com/indutny/uv_link_t@^1.0.0 [gpg] => uv_link_t.gyp:uv_link_t",
      "git://github.com/indutny/uv_proxy_t#master => uv_proxy_t.gyp:uv_proxy_t",
    ],
  },

  "targets": [ {
    "target_name": "name-dancer",
    "type": "executable",

    "dependencies": [
      "<!@(gypkg deps <(gypkg_deps))",
    ],

    "include_dirs": [
      ".",
    ],

    "sources": [
      "src/cli.c",
      "src/dancer.c",
      "src/parser.c",
    ],
  } ],
}
