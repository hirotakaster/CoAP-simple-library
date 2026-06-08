# Unit tests

Host-side unit tests for the `coap-simple` library. They compile the real
library (`../coap-simple.cpp`) against a small mocked Arduino environment, so
no board or toolchain beyond a C++ compiler is required.

## Running locally

```sh
cd test
make test
```

This builds `runner` and executes it. The process exits non-zero if any check
fails (which is what the CI uses to fail the build).

## Layout

| File | Purpose |
| --- | --- |
| `mocks/Arduino.h` | Minimal `String`, `IPAddress`, and a test-controllable `millis()`. |
| `mocks/Udp.h` | The abstract `UDP` interface the library talks to. |
| `FakeUDP.h` | UDP test double: captures sent datagrams, injects received ones. |
| `CoapCodec.h` | Helpers to decode/encode CoAP datagrams for assertions. |
| `TestHarness.h` | Shared assertion macros (`CHECK`, `CHECK_EQ`, `RUN`). |
| `test_main.cpp` | Library unit tests + `main()`. |
| `test_examples.cpp` | Integration tests mirroring `examples/` usage. |

## Coverage

**Library unit tests** (`test_main.cpp`) exercise packet/option encoding and the
read accessors (`CoapPacket`: `addOption`, `getOption`, `getContentFormat`,
`getUriPath`, `getQuery`, `isObserve`, `getObserveValue`), client requests
(`get`/`put`/`post`/`del`/`send`, query parsing, tokens, content-format), empty
messages (`ping`, `sendReset`), response building (`sendResponse`,
`sendObserveResponse`, observe uint encoding), incoming packet handling and
dispatch (`loop`, NOT_FOUND, ACK/response callbacks, CoAP-ping → Reset, malformed
input), and the Observe machinery (`addObserver`, `removeObserver`, `notify`,
sequence numbering, lease expiry, capacity limits).

**Example integration tests** (`test_examples.cpp`) reproduce the actual
callbacks from `examples/` and drive a full request → callback → response
round-trip through `loop()`:

- the `light` endpoint (`coapserver`, `esp32`, `esp8266`): PUT `"1"`/`"0"`
  toggles LED state and returns the new state; GET reports current state;
- the client GET + `response()` callback flow (`coaptest`): a GET is emitted,
  the server's ACK is fed back, and the response callback receives the payload;
- multiple/nested endpoints (e.g. `env/temp`);
- the Observe `subscribe` endpoint (`coapserver-with-observe`): plain GET,
  register (Observe=0) + `notify()` delivery with token and sequence,
  unsubscribe (Observe=1), and invalid Observe value → 4.02 Bad Option.

Run automatically on pull requests to `master` via
`.github/workflows/unit-tests.yml`.
