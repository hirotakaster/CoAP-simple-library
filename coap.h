/*
CoAP library for Core/Photon.
This software is released under the MIT License.
Copyright (c) 2014 Hirotaka Niisato
Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:
   
The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef __SIMPLE_COAP_H__
#define __SIMPLE_COAP_H__

#if defined(ARDUINO)
#include "Udp.h"
#define MAX_CALLBACK 10
#elif defined(SPARK)
#undef min
#undef max
#include <map>
#include "spark_wiring_string.h"
#include "spark_wiring_usbserial.h"
#include "spark_wiring_udp.h"
#include "spark_wiring_ipaddress.h"
#endif

#define COAP_HEADER_SIZE 4
#define COAP_OPTION_HEADER_SIZE 1
#define COAP_PAYLOAD_MARKER 0xFF
#define MAX_OPTION_NUM 10
#define BUF_MAX_SIZE 50
#define COAP_DEFAULT_PORT 5683

#define RESPONSE_CODE(class, detail) ((class << 5) | (detail))
#define COAP_OPTION_DELTA(v, n) (v < 13 ? (*n = (0xFF & v)) : (v <= 0xFF + 13 ? (*n = 13) : (*n = 14)))

typedef enum {
    COAP_CON = 0,
    COAP_NONCON = 1,
    COAP_ACK = 2,
    COAP_RESET = 3
} COAP_TYPE;

typedef enum {
    COAP_GET = 1,
    COAP_POST = 2,
    COAP_PUT = 3,
    COAP_DELETE = 4
} COAP_METHOD;

typedef enum {
    COAP_CREATED = RESPONSE_CODE(2, 1),
    COAP_DELETED = RESPONSE_CODE(2, 2),
    COAP_VALID = RESPONSE_CODE(2, 3),
    COAP_CHANGED = RESPONSE_CODE(2, 4),
    COAP_CONTENT = RESPONSE_CODE(2, 5),
    COAP_BAD_REQUEST = RESPONSE_CODE(4, 0),
    COAP_UNAUTHORIZED = RESPONSE_CODE(4, 1),
    COAP_BAD_OPTION = RESPONSE_CODE(4, 2),
    COAP_FORBIDDEN = RESPONSE_CODE(4, 3),
    COAP_NOT_FOUNT = RESPONSE_CODE(4, 4),
    COAP_METHOD_NOT_ALLOWD = RESPONSE_CODE(4, 5),
    COAP_NOT_ACCEPTABLE = RESPONSE_CODE(4, 6),
    COAP_PRECONDITION_FAILED = RESPONSE_CODE(4, 12),
    COAP_REQUEST_ENTITY_TOO_LARGE = RESPONSE_CODE(4, 13),
    COAP_UNSUPPORTED_CONTENT_FORMAT = RESPONSE_CODE(4, 15),
    COAP_INTERNAL_SERVER_ERROR = RESPONSE_CODE(5, 0),
    COAP_NOT_IMPLEMENTED = RESPONSE_CODE(5, 1),
    COAP_BAD_GATEWAY = RESPONSE_CODE(5, 2),
    COAP_SERVICE_UNAVALIABLE = RESPONSE_CODE(5, 3),
    COAP_GATEWAY_TIMEOUT = RESPONSE_CODE(5, 4),
    COAP_PROXYING_NOT_SUPPORTED = RESPONSE_CODE(5, 5)
} COAP_RESPONSE_CODE;

typedef enum {
    COAP_IF_MATCH = 1,
    COAP_URI_HOST = 3,
    COAP_E_TAG = 4,
    COAP_IF_NONE_MATCH = 5,
    COAP_URI_PORT = 7,
    COAP_LOCATION_PATH = 8,
    COAP_URI_PATH = 11,
    COAP_CONTENT_FORMAT = 12,
    COAP_MAX_AGE = 14,
    COAP_URI_QUERY = 15,
    COAP_ACCEPT = 17,
    COAP_LOCATION_QUERY = 20,
    COAP_PROXY_URI = 35,
    COAP_PROXY_SCHEME = 39
} COAP_OPTION_NUMBER;

typedef enum {
    COAP_NONE = -1,
    COAP_TEXT_PLAIN = 0,
    COAP_APPLICATION_LINK_FORMAT = 40,
    COAP_APPLICATION_XML = 41,
    COAP_APPLICATION_OCTET_STREAM = 42,
    COAP_APPLICATION_EXI = 47,
    COAP_APPLICATION_JSON = 50
} COAP_CONTENT_TYPE;

class CoapOption {
    public:
    uint8_t number;
    uint8_t length;
    uint8_t *buffer;
};

class CoapPacket {
    public:
    uint8_t type;
    uint8_t code;
    uint8_t *token;
    uint8_t tokenlen;
    uint8_t *payload;
    uint8_t payloadlen;
    uint16_t messageid;
    
    uint8_t optionnum;
    CoapOption options[MAX_OPTION_NUM];
};
typedef void (*callback)(CoapPacket &, IPAddress, int);

#if defined(ARDUINO)
class CoapUri {
    private:
        String u[MAX_CALLBACK];
        callback c[MAX_CALLBACK];
    public:
        CoapUri() {
            for (int i = 0; i < MAX_CALLBACK; i++) {
                u[i] = "";
                c[i] = NULL;
            }
        };
        void add(callback call, String url) {
            for (int i = 0; i < MAX_CALLBACK; i++)
                if (c[i] != NULL && u[i].equals(url)) {
                    c[i] = call;
                    return ;
                }
            for (int i = 0; i < MAX_CALLBACK; i++) {
                if (c[i] == NULL) {
                    c[i] = call;
                    u[i] = url;
                    return;
                }
            }
        };
        callback find(String url) {
            for (int i = 0; i < MAX_CALLBACK; i++) if (c[i] != NULL && u[i].equals(url)) return c[i];
            return NULL;
        } ;
};
#endif

class Coap {
    private:
        UDP *_udp;
#if defined(ARDUINO)
        CoapUri uri;
#elif defined(SPARK)
        std::map<String, callback> uri;
#endif
        callback resp;
        int _port;

        uint16_t sendPacket(CoapPacket &packet, IPAddress ip);
        uint16_t sendPacket(CoapPacket &packet, IPAddress ip, int port);
        int parseOption(CoapOption *option, uint16_t *running_delta, uint8_t **buf, size_t buflen);

    public:
        Coap(
#if defined(ARDUINO)
            UDP& udp
#endif
        );
        bool start();
        bool start(int port);
        void response(callback c) { resp = c; }
        
#if defined(ARDUINO)
        void server(callback c, String url) { uri.add(c, url); }
#elif defined(SPARK)
        void server(callback c, String url) { uri[url]  = c; }
#endif
        uint16_t sendResponse(IPAddress ip, int port, uint16_t messageid);
        uint16_t sendResponse(IPAddress ip, int port, uint16_t messageid, char *payload);
        uint16_t sendResponse(IPAddress ip, int port, uint16_t messageid, char *payload, int payloadlen);
        uint16_t sendResponse(IPAddress ip, int port, uint16_t messageid, char *payload, int payloadlen, COAP_RESPONSE_CODE code, COAP_CONTENT_TYPE type, uint8_t *token, int tokenlen);
        
        uint16_t get(IPAddress ip, int port, char *url);
        uint16_t put(IPAddress ip, int port, char *url, char *payload);
        uint16_t put(IPAddress ip, int port, char *url, char *payload, int payloadlen);
        uint16_t send(IPAddress ip, int port, char *url, COAP_TYPE type, COAP_METHOD method, uint8_t *token, uint8_t tokenlen, uint8_t *payload, uint32_t payloadlen);

        bool loop();
};

#endif
