# CoAP client, server library for Arduino.
<a href="http://coap.technology/" target=_blank>CoAP</a> simple server, client library for Arduino IDE/PlatformIO, ESP32, ESP8266.

## Source Code
This lightweight library's source code contains only 2 files. coap-simple.cpp, coap-simple.h.

## Example
Some sample sketches for Arduino included(/examples/).

 - coaptest.ino : simple request/response sample.
 - coapserver.ino : server endpoint url callback sample.
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
