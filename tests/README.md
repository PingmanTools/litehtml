# Native regression tests

These tests run without a platform UI or downloaded dependencies. Checks remain
active in Release builds.

```sh
cmake -S tests -B build/regressions -DCMAKE_BUILD_TYPE=Release
cmake --build build/regressions --parallel
ctest --test-dir build/regressions --output-on-failure
```

Run `parser_cleanup` under a leak detector: its exception paths must leave no
Gumbo parse trees allocated. On macOS:

```sh
leaks --atExit -- build/regressions/parser_cleanup
```

On Linux, configure with `-DCMAKE_C_FLAGS=-fsanitize=address`
`-DCMAKE_CXX_FLAGS=-fsanitize=address` and run with `ASAN_OPTIONS=detect_leaks=1`.
The existing external Cairo suite remains available through
`LITEHTML_BUILD_TESTING` in the main build.

Test the UTF-8-only configuration in a separate build directory:

```sh
cmake -S tests -B build/regressions-utf8 -DCMAKE_BUILD_TYPE=Release -DLITEHTML_UTF8_ONLY=ON
cmake --build build/regressions-utf8 --parallel
ctest --test-dir build/regressions-utf8 --output-on-failure
```
