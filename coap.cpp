#include "coap.h"
#include "Arduino.h"

#define LOGGING


bool Coap::start() {
    start(COAP_DEFAULT_PORT);
    return true;
}

bool Coap::start(int port) {
    _udp.begin(port);
    return true;
}

uint16_t Coap::sendPacket(CoapPacket &packet, const IPAddress& ip) {
    return sendPacket(packet, ip, COAP_DEFAULT_PORT);
}

uint16_t Coap::sendPacket(CoapPacket &packet, const IPAddress& ip, int port) {
    uint8_t buffer[BUF_MAX_SIZE];
    uint8_t *p = buffer;
    uint16_t running_delta = 0;
    uint16_t packetSize = 0;

    // make coap packet base header
    *p = 0x01 << 6;
    *p |= (packet.type & 0x03) << 4;
    *p++ |= (packet.tokenlen & 0x0F);
    *p++ = packet.code;
    *p++ = (packet.messageid >> 8);
    *p++ = (packet.messageid & 0xFF);
    p = buffer + COAP_HEADER_SIZE;
    packetSize += 4;

    // make token
    if (packet.token != NULL && packet.tokenlen <= 0x0F) {
        memcpy(p, packet.token, packet.tokenlen);
        p += packet.tokenlen;
        packetSize += packet.tokenlen;
    }

    // make option header
    for (int i = 0; i < packet.optionnum; i++)  {
        uint32_t optdelta;
        uint8_t len, delta;

        if (packetSize + 5 + packet.options[i].length >= BUF_MAX_SIZE) {
            return 0;
        }
        optdelta = packet.options[i].number - running_delta;
        COAP_OPTION_DELTA(optdelta, &delta);
        COAP_OPTION_DELTA((uint32_t)packet.options[i].length, &len);

        *p++ = (0xFF & (delta << 4 | len));
        if (delta == 13) {
            *p++ = (optdelta - 13);
            packetSize++;
        } else if (delta == 14) {
            *p++ = ((optdelta - 269) >> 8);
            *p++ = (0xFF & (optdelta - 269));
            packetSize+=2;
        } if (len == 13) {
            *p++ = (packet.options[i].length - 13);
            packetSize++;
        } else if (len == 14) {
            *p++ = (packet.options[i].length >> 8);
            *p++ = (0xFF & (packet.options[i].length - 269));
            packetSize+=2;
        }

        memcpy(p, packet.options[i].buffer, packet.options[i].length);
        p += packet.options[i].length;
        packetSize += packet.options[i].length + 1;
        running_delta = packet.options[i].number;
    }

    // make payload
    if (packet.payloadlen > 0) {
        if ((packetSize + 1 + packet.payloadlen) >= BUF_MAX_SIZE) {
            return 0;
        }
        *p++ = 0xFF;
        memcpy(p, packet.payload, packet.payloadlen);
        packetSize += 1 + packet.payloadlen;
    }

    _udp.beginPacket(ip, port);
    _udp.write(buffer, packetSize);
    _udp.endPacket();

    return packet.messageid;
}

uint16_t Coap::get(const IPAddress& ip, int port, const char *url) {
    return send(ip, port, url, COAP_CON, COAP_GET, NULL, 0, NULL, 0);
}

uint16_t Coap::put(const IPAddress& ip, int port, const char *url, const char *payload) {
    return send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (const uint8_t *)payload, strlen(payload));
}

uint16_t Coap::put(const IPAddress& ip, int port, const char *url, const char *payload, int payloadlen) {
    return send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (const uint8_t *)payload, payloadlen);
}

uint16_t Coap::send(const IPAddress& ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, uint32_t payloadlen) {

    // make packet
    CoapPacket packet;

    packet.type = type;
    packet.code = method;
    packet.token = token;
    packet.tokenlen = tokenlen;
    packet.payload = payload;
    packet.payloadlen = payloadlen;
    packet.optionnum = 0;
    packet.messageid = rand();

    // use URI_HOST UIR_PATH
    char ipaddress[16];
    sprintf(ipaddress,"%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    packet.options[packet.optionnum].buffer = (uint8_t*)ipaddress;
    packet.options[packet.optionnum].length = strlen(ipaddress);
    packet.options[packet.optionnum].number = COAP_URI_HOST;
    packet.optionnum++;

    // parse url
    int idx = 0;
    for (int i = 0; i < strlen(url); i++) {
        if (url[i] == '/') {
            packet.options[packet.optionnum].buffer = (uint8_t *)(url + idx);
            packet.options[packet.optionnum].length = i - idx;
            packet.options[packet.optionnum].number = COAP_URI_PATH;
            packet.optionnum++;
            idx = i + 1;
        }
    }

    if (idx <= strlen(url)) {
        packet.options[packet.optionnum].buffer = (uint8_t *)(url + idx);
        packet.options[packet.optionnum].length = strlen(url) - idx;
        packet.options[packet.optionnum].number = COAP_URI_PATH;
        packet.optionnum++;
    }

    // send packet
    return sendPacket(packet, ip, port);
}

int Coap::parseOption(CoapOption *option, uint16_t *running_delta, uint8_t **buf, size_t buflen) {
    uint8_t *p = *buf;
    uint8_t headlen = 1;
    uint16_t len, delta;

    if (buflen < headlen) return -1;

    delta = (p[0] & 0xF0) >> 4;
    len = p[0] & 0x0F;

    if (delta == 13) {
        headlen++;
        if (buflen < headlen) return -1;
        delta = p[1] + 13;
        p++;
    } else if (delta == 14) {
        headlen += 2;
        if (buflen < headlen) return -1;
        delta = ((p[1] << 8) | p[2]) + 269;
        p+=2;
    } else if (delta == 15) return -1;

    if (len == 13) {
        headlen++;
        if (buflen < headlen) return -1;
        len = p[1] + 13;
        p++;
    } else if (len == 14) {
        headlen += 2;
        if (buflen < headlen) return -1;
        len = ((p[1] << 8) | p[2]) + 269;
        p+=2;
    } else if (len == 15)
        return -1;

    if ((p + 1 + len) > (*buf + buflen))  return -1;
    option->number = delta + *running_delta;
    option->buffer = p+1;
    option->length = len;
    *buf = p + 1 + len;
    *running_delta += delta;

    return 0;
}

bool Coap::loop() {
    uint8_t buffer[BUF_MAX_SIZE];
    int32_t packetlen = _udp.parsePacket();

    while (packetlen > 0) {
	// We reduce the read size by one to make space for a terminating '\0' (see below)
        const auto READ_SIZE = BUF_MAX_SIZE - 1;
        packetlen = _udp.read(buffer, packetlen >= READ_SIZE ? READ_SIZE : packetlen);

        CoapPacket packet;

        // parse coap packet header
        if (packetlen < COAP_HEADER_SIZE || (((buffer[0] & 0xC0) >> 6) != 1)) {
            packetlen = _udp.parsePacket();
            continue;
        }

        packet.type = (buffer[0] & 0x30) >> 4;
        packet.tokenlen = buffer[0] & 0x0F;
        packet.code = buffer[1];
        packet.messageid = 0xFF00 & (buffer[2] << 8);
        packet.messageid |= 0x00FF & buffer[3];

        if (packet.tokenlen == 0)  packet.token = NULL;
        else if (packet.tokenlen <= 8)  packet.token = buffer + 4;
        else {
            packetlen = _udp.parsePacket();
            continue;
        }

        // parse packet options/payload
        if (COAP_HEADER_SIZE + packet.tokenlen < packetlen) {
            int optionIndex = 0;
            uint16_t delta = 0;
            uint8_t *end = buffer + packetlen;
            uint8_t *p = buffer + COAP_HEADER_SIZE + packet.tokenlen;
            while(optionIndex < MAX_OPTION_NUM && *p != 0xFF && p < end) {
                packet.options[optionIndex];
                if (0 != parseOption(&packet.options[optionIndex], &delta, &p, end-p))
                    return false;
                optionIndex++;
            }
            packet.optionnum = optionIndex;

            if (p+1 < end && *p == 0xFF) {
                packet.payload = p+1;
                packet.payloadlen = end-(p+1);
		// Adding '\0' at the end of buffer.
		p[packet.payloadlen] = '\0';
            } else {
                packet.payload = NULL;
                packet.payloadlen= 0;
            }
        }

        if (packet.type == COAP_ACK) {
            // call response function
            resp(packet, _udp.remoteIP(), _udp.remotePort());

        } else {
            int maxUrlLength = 0;
	    for (int i = 0; i < packet.optionnum; i++) {
                if (packet.options[i].number == COAP_URI_PATH && packet.options[i].length > 0) {
                    maxUrlLength += packet.options[i].length + 1;
		}
	    }
            char url[maxUrlLength + 1]; // + 1 for the terminating '\0';
	    int pos = 0;
            // call endpoint url function
            for (int i = 0; i < packet.optionnum; i++) {
                if (packet.options[i].number == COAP_URI_PATH && packet.options[i].length > 0) {
                    if(pos == 0)
                      url[pos++] = '/';
                    memcpy(&url[pos], packet.options[i].buffer, packet.options[i].length);
		    pos += packet.options[i].length;
                }
            }        
	    url[pos] = '\0';

	    auto callback = uri.find(url);

            if (!callback) {
                sendResponse(_udp.remoteIP(), _udp.remotePort(), packet.messageid, NULL, 0,
                        COAP_NOT_FOUNT, COAP_NONE, NULL, 0);
            } else {
                callback(packet, _udp.remoteIP(), _udp.remotePort());
            }
        }

        /* this type check did not use.
        if (packet.type == COAP_CON) {
            // send response 
             sendResponse(_udp.remoteIP(), _udp.remotePort(), packet.messageid);
        }
         */

        // next packet
        packetlen = _udp.parsePacket();
    }

    return true;
}

uint16_t Coap::sendResponse(const IPAddress& ip, int port, uint16_t messageid) {
    sendResponse(ip, port, messageid, NULL, 0, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(const IPAddress& ip, int port, uint16_t messageid, const char *payload) {
    sendResponse(ip, port, messageid, payload, strlen(payload), COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(const IPAddress& ip, int port, uint16_t messageid, const char *payload, int payloadlen) {
    sendResponse(ip, port, messageid, payload, payloadlen, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}


uint16_t Coap::sendResponse(const IPAddress& ip, int port, uint16_t messageid, const char *payload, int payloadlen,
                COAP_RESPONSE_CODE code, COAP_CONTENT_TYPE type, const uint8_t *token, int tokenlen) {
    // make packet
    CoapPacket packet;

    packet.type = COAP_ACK;
    packet.code = code;
    packet.token = token;
    packet.tokenlen = tokenlen;
    packet.payload = (uint8_t *)payload;
    packet.payloadlen = payloadlen;
    packet.optionnum = 0;
    packet.messageid = messageid;

    // if more options?
    char optionBuffer[2];
    optionBuffer[0] = ((uint16_t)type & 0xFF00) >> 8;
    optionBuffer[1] = ((uint16_t)type & 0x00FF) ;
    packet.options[packet.optionnum].buffer = (uint8_t *)optionBuffer;
    packet.options[packet.optionnum].length = 2;
    packet.options[packet.optionnum].number = COAP_CONTENT_FORMAT;
    packet.optionnum++;

    return sendPacket(packet, ip, port);
}
