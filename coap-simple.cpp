#include "coap-simple.h"
#include "Arduino.h"

#define LOGGING

void CoapPacket::addOption(uint8_t number, uint8_t length, uint8_t *opt_payload)
{
    options[optionnum].number = number;
    options[optionnum].length = length;
    options[optionnum].buffer = opt_payload;

    ++optionnum;
}


Coap::Coap(
    UDP& udp,
    int coap_buf_size  /* default value is COAP_BUF_MAX_SIZE */
) {
    this->_udp = &udp;
    this->coap_buf_size = coap_buf_size;
    this->tx_buffer = new uint8_t[this->coap_buf_size];
    this->rx_buffer = new uint8_t[this->coap_buf_size];
}

Coap::~Coap() {
    if (this->tx_buffer != NULL)
      delete[] this->tx_buffer;

    if (this->rx_buffer != NULL)
      delete[] this->rx_buffer;
}

bool Coap::start() {
    this->start(COAP_DEFAULT_PORT);
    return true;
}

bool Coap::start(int port) {
    this->_udp->begin(port);
    return true;
}

uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip) {
    return this->sendPacket(packet, ip, COAP_DEFAULT_PORT);
}

uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip, int port) {
    uint8_t *p = this->tx_buffer;
    uint16_t running_delta = 0;
    uint16_t packetSize = 0;

    // make coap packet base header
    *p = 0x01 << 6;
    *p |= (packet.type & 0x03) << 4;
    *p++ |= (packet.tokenlen & 0x0F);
    *p++ = packet.code;
    *p++ = (packet.messageid >> 8);
    *p++ = (packet.messageid & 0xFF);
    p = this->tx_buffer + COAP_HEADER_SIZE;
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

        if (packetSize + 5 + packet.options[i].length >= coap_buf_size) {
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
        if ((packetSize + 1 + packet.payloadlen) >= coap_buf_size) {
            return 0;
        }
        *p++ = 0xFF;
        memcpy(p, packet.payload, packet.payloadlen);
        packetSize += 1 + packet.payloadlen;
    }

    _udp->beginPacket(ip, port);
    _udp->write(this->tx_buffer, packetSize);
    _udp->endPacket();

    return packet.messageid;
}

uint16_t Coap::get(IPAddress ip, int port, const char *url) {
    return this->send(ip, port, url, COAP_CON, COAP_GET, NULL, 0, NULL, 0);
}

uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload) {
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, strlen(payload));
}

uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen) {
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, payloadlen);
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen) {
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, COAP_NONE);
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen, COAP_CONTENT_TYPE content_type) {
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, content_type, rand());
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen, COAP_CONTENT_TYPE content_type, uint16_t messageid) {

    // make packet
    CoapPacket packet;

    packet.type = type;
    packet.code = method;
    packet.token = token;
    packet.tokenlen = tokenlen;
    packet.payload = payload;
    packet.payloadlen = payloadlen;
    packet.optionnum = 0;
    packet.messageid = messageid;

    // use URI_HOST UIR_PATH
    char ipaddress[16] = "";
    sprintf(ipaddress, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    packet.addOption(COAP_URI_HOST, strlen(ipaddress), (uint8_t *)ipaddress);

    /*
        Add Query Support
        Author: @YelloooBlue
    */

    // parse url
    size_t idx = 0;
    bool hasQuery = false;
    for (size_t i = 0; i < strlen(url); i++) {
        // The reserved characters "/"  "?"  "&"
        if (url[i] == '/') {
            packet.addOption(COAP_URI_PATH, i-idx, (uint8_t *)(url + idx)); //one URI_PATH (terminated by '/')
            idx = i + 1;
        } else if (url[i] == '?' && !hasQuery) {
            packet.addOption(COAP_URI_PATH, i-idx, (uint8_t *)(url + idx)); //the last URI_PATH (between / and ?)
            hasQuery = true; //now start to parse the query
            idx = i + 1;
        } else if (url[i] == '&' && hasQuery) {
            packet.addOption(COAP_URI_QUERY, i-idx, (uint8_t *)(url + idx)); //one URI_QUERY (terminated by '&')
            idx = i + 1;
        }
    }

    if (idx <= strlen(url)) {
        if (hasQuery) {
            packet.addOption(COAP_URI_QUERY, strlen(url)-idx, (uint8_t *)(url + idx)); //the last URI_QUERY (between &/? and the end)
        } else {
            packet.addOption(COAP_URI_PATH, strlen(url)-idx, (uint8_t *)(url + idx)); //the last URI_PATH (between / and the end)
        }
    }
    

    /*
        Adding query support ends
        Date: 2024.03.03
    */

	// if Content-Format option
	uint8_t optionBuffer[2] {0};
	if (content_type != COAP_NONE) {
		optionBuffer[0] = ((uint16_t)content_type & 0xFF00) >> 8;
		optionBuffer[1] = ((uint16_t)content_type & 0x00FF) ;
		packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);
	}

    // send packet
    return this->sendPacket(packet, ip, port);
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
    int32_t packetlen = _udp->parsePacket();

    while (packetlen > 0) {
        packetlen = _udp->read(this->rx_buffer, packetlen >= coap_buf_size ? coap_buf_size : packetlen);

        CoapPacket packet;

        // parse coap packet header
        if (packetlen < COAP_HEADER_SIZE || (((this->rx_buffer[0] & 0xC0) >> 6) != 1)) {
            packetlen = _udp->parsePacket();
            continue;
        }

        packet.type = (this->rx_buffer[0] & 0x30) >> 4;
        packet.tokenlen = this->rx_buffer[0] & 0x0F;
        packet.code = this->rx_buffer[1];
        packet.messageid = 0xFF00 & (this->rx_buffer[2] << 8);
        packet.messageid |= 0x00FF & this->rx_buffer[3];

        if (packet.tokenlen == 0)  packet.token = NULL;
        else if (packet.tokenlen <= 8)  packet.token = this->rx_buffer + 4;
        else {
            packetlen = _udp->parsePacket();
            continue;
        }

        // parse packet options/payload
        if (COAP_HEADER_SIZE + packet.tokenlen < packetlen) {
            int optionIndex = 0;
            uint16_t delta = 0;
            uint8_t *end = this->rx_buffer + packetlen;
            uint8_t *p = this->rx_buffer + COAP_HEADER_SIZE + packet.tokenlen;
            while(optionIndex < COAP_MAX_OPTION_NUM && *p != 0xFF && p < end) {
                //packet.options[optionIndex];
                if (0 != parseOption(&packet.options[optionIndex], &delta, &p, end-p))
                    return false;
                optionIndex++;
            }
            packet.optionnum = optionIndex;

            if (p+1 < end && *p == 0xFF) {
                packet.payload = p+1;
                packet.payloadlen = end-(p+1);
            } else {
                packet.payload = NULL;
                packet.payloadlen= 0;
            }
        }

        if (packet.type == COAP_ACK) {
            // call response function
            resp(packet, _udp->remoteIP(), _udp->remotePort());

        } else {

            String url = "";
            // call endpoint url function
            for (int i = 0; i < packet.optionnum; i++) {
                if (packet.options[i].number == COAP_URI_PATH && packet.options[i].length > 0) {
                    char urlname[packet.options[i].length + 1];
                    memcpy(urlname, packet.options[i].buffer, packet.options[i].length);
                    urlname[packet.options[i].length] = 0;
                    if(url.length() > 0)
                      url += "/";
                    url += (const char *)urlname;
                }
            }

            if (!uri.find(url)) {
                sendResponse(_udp->remoteIP(), _udp->remotePort(), packet.messageid, NULL, 0,
                        COAP_NOT_FOUNT, COAP_NONE, NULL, 0);
            } else {
                uri.find(url)(packet, _udp->remoteIP(), _udp->remotePort());
            }
        }

        /* this type check did not use.
        if (packet.type == COAP_CON) {
            // send response
             sendResponse(_udp->remoteIP(), _udp->remotePort(), packet.messageid);
        }
         */

        // next packet
        packetlen = _udp->parsePacket();
    }

    return true;
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid) {
    return this->sendResponse(ip, port, messageid, NULL, 0, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload) {
    return this->sendResponse(ip, port, messageid, payload, strlen(payload), COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload, size_t payloadlen) {
    return this->sendResponse(ip, port, messageid, payload, payloadlen, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}


uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload, size_t payloadlen,
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
    uint8_t optionBuffer[2] = {0};
    optionBuffer[0] = ((uint16_t)type & 0xFF00) >> 8;
    optionBuffer[1] = ((uint16_t)type & 0x00FF) ;
	packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);

    return this->sendPacket(packet, ip, port);
}
