# CoAP client, server library for Arduino.
<a href="http://coap.technology/" target=_blank>CoAP</a> simple server, client library for Arduino IDE/PlatformIO, ESP32, ESP8266.

## Why this fork exists

This fork preserves the complete upstream `hirotakaster/CoAP-simple-library` repository while repairing confirmable-message delivery needed by systems that use CoAP as a reliable transport foundation. The repaired and documented snapshot is pinned at tag `v1.4.0-sslogic-reliable-con.2`; later upstream changes are not included unless they are reviewed and deliberately merged.

Upstream `master` provides the lightweight Arduino CoAP client/server, URI callbacks, request/response helpers, ESP32/ESP8266 examples, and non-confirmable Observe notifications. Its `put()` method marks requests as confirmable (`COAP_CON`), but the upstream implementation does not automatically acknowledge valid CON requests, retain sent CON packets, retransmit packets whose ACK is missing, or report delivery timeout. Its normal `put()` API also does not provide a distinct convenience method for non-confirmable PUT traffic.

This fork adds:

- automatic CoAP responses for valid confirmable requests and `4.04 Not Found` responses for unknown confirmable resources;
- a bounded per-`Coap` pending-CON table that keeps the encoded packet until delivery finishes;
- ACK and Reset matching by message ID, peer IP, and peer port;
- exponential-backoff retransmission followed by a reported timeout;
- an `onDelivery()` callback for acknowledged, timed-out, and reset deliveries;
- explicit `putNonConfirmable()` overloads for replaceable status and telemetry traffic;
- clean refusal when the bounded pending table is full, so a CON packet is never sent while falsely appearing to be tracked; and
- `repair_coap_simple_reliability.ps1`, the one-shot script used to apply and verify the source repair.

A CoAP ACK proves that the remote CoAP endpoint received the packet. It does not prove that an application accepted, persisted, or completed the requested operation. Pairing, configuration, and other durable workflows must still return an application-level result and safely handle duplicate requests.

### Use this fixed version

```bash
git clone https://github.com/sslogic/CoAP-simple-library
cd CoAP-simple-library
git checkout v1.4.0-sslogic-reliable-con.2
```

## Source Code
This lightweight library's source code contains only 2 files. coap-simple.cpp, coap-simple.h.

## Example
Some sample sketches for Arduino included(/examples/).

 - coaptest.ino : simple request/response sample.
 - coapserver.ino : server endpoint url callback sample.
 - coapserver-with-observe.ino : observe sample (experimental; max observers is COAP_MAX_OBSERVERS, observers expire after COAP_OBSERVER_LEASE_MS, full table is refused).
 - esp32.ino, esp8266.ino : server endpoint url callback/response.

## How to use
Download this source code branch zip file and extract it to the Arduino libraries directory or checkout repository. Here is checkout on MacOS X.

```bash
cd $HOME/Documents/Arduino/libraries/
git clone https://github.com/hirotakaster/CoAP-simple-library
# restart Arduino IDE, you can find CoAP-simple-library examples.
```

These examples need CoAP server libcoap or microcoap server to test the example program. 

This is  how to use the example with libcoap on Ubuntu Linux. You don't need to use CoAP server(request/reseponse), simply follow these steps :

```bash
git clone https://github.com/obgm/libcoap 
cd libcoap/
./autogen.sh 
./configure
make
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.libs
gcc -o coap-server ./examples/coap-server.c -I./include -I. -L.libs -lcoap-1 -DWITH_POSIX
gcc -o coap-client ./examples/client.c ./examples/coap_list.c -I./include -I. -L.libs -lcoap-1 -DWITH_POSIX
./coap-server
# next start Arduino and check the request/response.
```

## Particle Photon, Core compatible
Check <a href="https://github.com/hirotakaster/CoAP">this</a> version of the library for Particle Photon, Core compatibility.
