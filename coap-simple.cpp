#include "coap-simple.h"
#include "Arduino.h"

#define LOGGING

void CoapPacket::addOption(uint8_t number, uint8_t length, uint8_t *opt_payload)
{
    if (optionnum >= COAP_MAX_OPTION_NUM)
    {
        return;
    }
    options[optionnum].number = number;
    options[optionnum].length = length;
    options[optionnum].buffer = opt_payload;

    ++optionnum;
}

bool CoapPacket::isObserve()
{
    for (int i = 0; i < optionnum; i++)
    {
        if (options[i].number == COAP_OBSERVE)
        {
            return true;
        }
    }
    return false;
}

bool CoapPacket::getObserveValue(uint32_t &value)
{
    for (int i = 0; i < optionnum; i++)
    {
        if (options[i].number != COAP_OBSERVE)
            continue;
        if (options[i].length > 3)
            return false;
        uint32_t v = 0;
        for (uint8_t j = 0; j < options[i].length; j++)
        {
            v = (v << 8) | options[i].buffer[j];
        }
        value = v;
        return true;
    }
    return false;
}

Coap::Coap(
    UDP &udp,
    int coap_buf_size /* default value is COAP_BUF_MAX_SIZE */,
    int resp_buf_size /* default: same as coap_buf_size */
)
{
    this->_udp = &udp;
    this->coap_buf_size = coap_buf_size;
    if (resp_buf_size < 0) resp_buf_size = coap_buf_size;
    this->resp_buf_size = resp_buf_size;
    this->tx_buffer = new uint8_t[this->coap_buf_size];
    this->resp_buffer = new uint8_t[this->resp_buf_size];
    this->rx_buffer = new uint8_t[this->coap_buf_size];
}

Coap::~Coap()
{
    if (this->tx_buffer != NULL)
        delete[] this->tx_buffer;

        if (this->resp_buffer != NULL)
            delete[] this->resp_buffer;

    if (this->rx_buffer != NULL)
        delete[] this->rx_buffer;
}

bool Coap::start()
{
    this->start(COAP_DEFAULT_PORT);
    return true;
}

bool Coap::start(int port)
{
    this->_udp->begin(port);
    return true;
}

uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip) {
    return this->sendPacket(packet, ip, COAP_DEFAULT_PORT, this->tx_buffer, this->coap_buf_size);
}

uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip, int port) {
    return this->sendPacket(packet, ip, port, this->tx_buffer, this->coap_buf_size);
}

uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip, int port, uint8_t *buffer, size_t buffer_size) {
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
        if (packetSize + packet.tokenlen > buffer_size) return 0;
        memcpy(p, packet.token, packet.tokenlen);
        p += packet.tokenlen;
        packetSize += packet.tokenlen;
    }

    // make option header
    for (int i = 0; i < packet.optionnum; i++)
    {
        uint32_t optdelta;
        uint8_t len, delta;

        if (packetSize + 5 + packet.options[i].length >= buffer_size) {
            return 0;
        }
        optdelta = packet.options[i].number - running_delta;
        COAP_OPTION_DELTA(optdelta, &delta);
        COAP_OPTION_DELTA((uint32_t)packet.options[i].length, &len);

        *p++ = (0xFF & (delta << 4 | len));
        if (delta == 13)
        {
            *p++ = (optdelta - 13);
            packetSize++;
        }
        else if (delta == 14)
        {
            *p++ = ((optdelta - 269) >> 8);
            *p++ = (0xFF & (optdelta - 269));
            packetSize += 2;
        }
        if (len == 13)
        {
            *p++ = (packet.options[i].length - 13);
            packetSize++;
        }
        else if (len == 14)
        {
            *p++ = (packet.options[i].length >> 8);
            *p++ = (0xFF & (packet.options[i].length - 269));
            packetSize += 2;
        }

        memcpy(p, packet.options[i].buffer, packet.options[i].length);
        p += packet.options[i].length;
        packetSize += packet.options[i].length + 1;
        running_delta = packet.options[i].number;
    }

    // make payload
    if (packet.payloadlen > 0) {
        if ((packetSize + 1 + packet.payloadlen) >= buffer_size) {
            return 0;
        }
        *p++ = 0xFF;
        memcpy(p, packet.payload, packet.payloadlen);
        packetSize += 1 + packet.payloadlen;
    }

    _udp->beginPacket(ip, port);
    _udp->write(buffer, packetSize);
    _udp->endPacket();

    return packet.messageid;
}

uint16_t Coap::get(IPAddress ip, int port, const char *url)
{
    return this->send(ip, port, url, COAP_CON, COAP_GET, NULL, 0, NULL, 0);
}

uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload)
{
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, strlen(payload));
}

uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen)
{
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, payloadlen);
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen)
{
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, COAP_NONE);
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen, COAP_CONTENT_TYPE content_type)
{
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, content_type, rand());
}

uint16_t Coap::send(IPAddress ip, int port, const char *url, COAP_TYPE type, COAP_METHOD method, const uint8_t *token, uint8_t tokenlen, const uint8_t *payload, size_t payloadlen, COAP_CONTENT_TYPE content_type, uint16_t messageid)
{

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

    // use URI_HOST URI_PATH
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
    for (size_t i = 0; i < strlen(url); i++)
    {
        // The reserved characters "/"  "?"  "&"
        if (url[i] == '/')
        {
            packet.addOption(COAP_URI_PATH, i - idx, (uint8_t *)(url + idx)); // one URI_PATH (terminated by '/')
            idx = i + 1;
        }
        else if (url[i] == '?' && !hasQuery)
        {
            packet.addOption(COAP_URI_PATH, i - idx, (uint8_t *)(url + idx)); // the last URI_PATH (between / and ?)
            hasQuery = true;                                                  // now start to parse the query
            idx = i + 1;
        }
        else if (url[i] == '&' && hasQuery)
        {
            packet.addOption(COAP_URI_QUERY, i - idx, (uint8_t *)(url + idx)); // one URI_QUERY (terminated by '&')
            idx = i + 1;
        }
    }

    if (idx <= strlen(url))
    {
        if (hasQuery)
        {
            packet.addOption(COAP_URI_QUERY, strlen(url) - idx, (uint8_t *)(url + idx)); // the last URI_QUERY (between &/? and the end)
        }
        else
        {
            packet.addOption(COAP_URI_PATH, strlen(url) - idx, (uint8_t *)(url + idx)); // the last URI_PATH (between / and the end)
        }
    }

    /*
        Adding query support ends
        Date: 2024.03.03
    */

    // if Content-Format option
    uint8_t optionBuffer[2]{0};
    if (content_type != COAP_NONE)
    {
        optionBuffer[0] = ((uint16_t)content_type & 0xFF00) >> 8;
        optionBuffer[1] = ((uint16_t)content_type & 0x00FF);
        packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);
    }

    // send packet
    return this->sendPacket(packet, ip, port);
}

int Coap::parseOption(CoapOption *option, uint16_t *running_delta, uint8_t **buf, size_t buflen)
{
    uint8_t *p = *buf;
    uint8_t headlen = 1;
    uint16_t len, delta;

    if (buflen < headlen)
        return -1;

    delta = (p[0] & 0xF0) >> 4;
    len = p[0] & 0x0F;

    if (delta == 13)
    {
        headlen++;
        if (buflen < headlen)
            return -1;
        delta = p[1] + 13;
        p++;
    }
    else if (delta == 14)
    {
        headlen += 2;
        if (buflen < headlen)
            return -1;
        delta = ((p[1] << 8) | p[2]) + 269;
        p += 2;
    }
    else if (delta == 15)
        return -1;

    if (len == 13)
    {
        headlen++;
        if (buflen < headlen)
            return -1;
        len = p[1] + 13;
        p++;
    }
    else if (len == 14)
    {
        headlen += 2;
        if (buflen < headlen)
            return -1;
        len = ((p[1] << 8) | p[2]) + 269;
        p += 2;
    }
    else if (len == 15)
        return -1;

    if ((p + 1 + len) > (*buf + buflen))
        return -1;
    option->number = delta + *running_delta;
    option->buffer = p + 1;
    option->length = len;
    *buf = p + 1 + len;
    *running_delta += delta;

    return 0;
}

bool Coap::loop()
{
    int32_t packetlen = _udp->parsePacket();

    while (packetlen > 0)
    {
        packetlen = _udp->read(this->rx_buffer, packetlen >= coap_buf_size ? coap_buf_size : packetlen);

        CoapPacket packet;

        // parse coap packet header
        if (packetlen < COAP_HEADER_SIZE || (((this->rx_buffer[0] & 0xC0) >> 6) != 1))
        {
            packetlen = _udp->parsePacket();
            continue;
        }

        packet.type = (this->rx_buffer[0] & 0x30) >> 4;
        packet.tokenlen = this->rx_buffer[0] & 0x0F;
        packet.code = this->rx_buffer[1];
        packet.messageid = 0xFF00 & (this->rx_buffer[2] << 8);
        packet.messageid |= 0x00FF & this->rx_buffer[3];

        if (packet.tokenlen == 0)
            packet.token = NULL;
        else if (packet.tokenlen <= 8)
            packet.token = this->rx_buffer + 4;
        else
        {
            packetlen = _udp->parsePacket();
            continue;
        }

        // parse packet options/payload
        if (COAP_HEADER_SIZE + packet.tokenlen < packetlen)
        {
            int optionIndex = 0;
            uint16_t delta = 0;
            uint8_t *end = this->rx_buffer + packetlen;
            uint8_t *p = this->rx_buffer + COAP_HEADER_SIZE + packet.tokenlen;
            while (optionIndex < COAP_MAX_OPTION_NUM && p < end && *p != 0xFF)
            {
                // packet.options[optionIndex];
                if (0 != parseOption(&packet.options[optionIndex], &delta, &p, end - p))
                    return false;
                optionIndex++;
            }
            packet.optionnum = optionIndex;
            if (p < end && *p == 0xFF)
            {
                packet.payload = p + 1;
                packet.payloadlen = end - (p + 1);
            }
            else
            {
                packet.payload = NULL;
                packet.payloadlen = 0;
            }
        }

        if (packet.type == COAP_ACK)
        {
            // call response function
            resp(packet, _udp->remoteIP(), _udp->remotePort());
        }
        else
        {

            String url = "";
            // call endpoint url function
            for (int i = 0; i < packet.optionnum; i++)
            {
                if (packet.options[i].number == COAP_URI_PATH && packet.options[i].length > 0)
                {
                    char urlname[packet.options[i].length + 1];
                    memcpy(urlname, packet.options[i].buffer, packet.options[i].length);
                    urlname[packet.options[i].length] = 0;
                    if (url.length() > 0)
                        url += "/";
                    url += (const char *)urlname;
                }
            }

            if (!uri.find(url))
            {
                sendResponse(_udp->remoteIP(), _udp->remotePort(), packet.messageid, NULL, 0,
                             COAP_NOT_FOUND, COAP_NONE, NULL, 0);
            }
            else
            {
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

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid)
{
    return this->sendResponse(ip, port, messageid, NULL, 0, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload)
{
    return this->sendResponse(ip, port, messageid, payload, strlen(payload), COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload, size_t payloadlen)
{
    return this->sendResponse(ip, port, messageid, payload, payloadlen, COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0);
}

uint16_t Coap::sendResponse(IPAddress ip, int port, uint16_t messageid, const char *payload, size_t payloadlen,
                            COAP_RESPONSE_CODE code, COAP_CONTENT_TYPE type, const uint8_t *token, int tokenlen)
{
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
    optionBuffer[1] = ((uint16_t)type & 0x00FF);
    packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);

    return this->sendPacket(packet, ip, port, this->resp_buffer, this->resp_buf_size);
}

void Coap::setResponseBufferSize(int size) {
    if (size <= 0) return;
    if (size == this->resp_buf_size) return;
    if (this->resp_buffer != NULL) delete[] this->resp_buffer;
    this->resp_buf_size = size;
    this->resp_buffer = new uint8_t[this->resp_buf_size];
}

static uint8_t encodeUintOption(uint32_t value, uint8_t out[3])
{
    if (value == 0)
    {
        // CoAP uint option encoding uses a zero-length option for value 0.
        return 0;
    }
    if (value <= 0xFF)
    {
        out[0] = (uint8_t)value;
        return 1;
    }
    if (value <= 0xFFFF)
    {
        out[0] = (uint8_t)(value >> 8);
        out[1] = (uint8_t)(value & 0xFF);
        return 2;
    }
    out[0] = (uint8_t)((value >> 16) & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)(value & 0xFF);
    return 3;
}

uint16_t Coap::sendObserveResponse(IPAddress ip, int port, uint16_t messageid, const char *payload, size_t payloadlen,
                                   COAP_RESPONSE_CODE code, COAP_CONTENT_TYPE type, const uint8_t *token, int tokenlen, uint32_t observe_seq)
{
    CoapPacket packet;

    packet.type = COAP_ACK;
    packet.code = code;
    packet.token = token;
    packet.tokenlen = tokenlen;
    packet.payload = (uint8_t *)payload;
    packet.payloadlen = payloadlen;
    packet.optionnum = 0;
    packet.messageid = messageid;

    uint8_t observeBuf[3] = {0};
    uint8_t observeLen = encodeUintOption(observe_seq, observeBuf);
    packet.addOption(COAP_OBSERVE, observeLen, observeBuf);

    uint8_t optionBuffer[2] = {0};
    optionBuffer[0] = ((uint16_t)type & 0xFF00) >> 8;
    optionBuffer[1] = ((uint16_t)type & 0x00FF);
    packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);

    return this->sendPacket(packet, ip, port);
}

Observer::Observer(IPAddress ip, int port, const uint8_t *token, int token_len)
    : ip(ip), port(port), token_len(token_len), counter(0)
{
    if (this->token_len > 8)
        this->token_len = 8;
    if (this->token_len > 0 && token != NULL)
        memcpy(this->token, token, this->token_len);
}

uint16_t Coap::notify(Observer *observer, const char *payload, int payload_len, COAP_CONTENT_TYPE type)
{
    CoapPacket packet;

    packet.type = COAP_NONCON; // Notifications are non-confirmable.
    packet.code = COAP_CONTENT;
    packet.token = observer->token;
    packet.tokenlen = observer->token_len;
    packet.payload = (uint8_t *)payload;
    packet.payloadlen = payload_len;
    packet.optionnum = 0;
    uint32_t observe_seq = ++observer->counter;
    packet.messageid = rand();

    uint8_t observeBuf[3] = {0};
    uint8_t observeLen = encodeUintOption(observe_seq, observeBuf);
    packet.addOption(COAP_OBSERVE, observeLen, observeBuf);

    uint8_t optionBuffer[2] = {0};
    optionBuffer[0] = ((uint16_t)type & 0xFF00) >> 8;
    optionBuffer[1] = ((uint16_t)type & 0x00FF);
    packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);

    return this->sendPacket(packet, observer->ip, observer->port);
}

static bool urlEquals(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return false;
    return strcmp(a, b) == 0;
}

static bool tokenEquals(const uint8_t *a, uint8_t alen, const uint8_t *b, uint8_t blen)
{
    if (alen != blen)
        return false;
    if (alen == 0)
        return true;
    if (a == NULL || b == NULL)
        return false;
    return memcmp(a, b, alen) == 0;
}

bool Coap::addObserver(const char *url, IPAddress ip, int port, const uint8_t *token, uint8_t tokenlen)
{
    if (url == NULL)
        return false;
    if (strlen(url) >= COAP_MAX_OBSERVE_URL_LEN)
        return false;
    if (tokenlen > 8)
        return false;

    unsigned long now = millis();

    for (int i = 0; i < COAP_MAX_OBSERVERS; i++)
    {
        if (!observers[i].in_use)
            continue;
        if (observers[i].ip == ip && observers[i].port == (uint16_t)port && urlEquals(observers[i].url, url) && tokenEquals(observers[i].token, observers[i].tokenlen, token, tokenlen))
        {
            observers[i].last_seen_ms = now;
            return true;
        }
    }

    for (int i = 0; i < COAP_MAX_OBSERVERS; i++)
    {
        if (!observers[i].in_use)
        {
            observers[i].in_use = true;
            observers[i].ip = ip;
            observers[i].port = (uint16_t)port;
            observers[i].tokenlen = tokenlen;
            if (tokenlen > 0 && token != NULL)
                memcpy(observers[i].token, token, tokenlen);
            observers[i].observe_seq = 0;
            observers[i].last_seen_ms = now;
            strncpy(observers[i].url, url, COAP_MAX_OBSERVE_URL_LEN - 1);
            observers[i].url[COAP_MAX_OBSERVE_URL_LEN - 1] = 0;
            return true;
        }
    }

    return false;
}

bool Coap::removeObserver(const char *url, IPAddress ip, int port, const uint8_t *token, uint8_t tokenlen)
{
    if (url == NULL)
        return false;
    bool removed = false;
    for (int i = 0; i < COAP_MAX_OBSERVERS; i++)
    {
        if (!observers[i].in_use)
            continue;
        if (observers[i].ip == ip && observers[i].port == (uint16_t)port && urlEquals(observers[i].url, url) && tokenEquals(observers[i].token, observers[i].tokenlen, token, tokenlen))
        {
            observers[i].in_use = false;
            observers[i].tokenlen = 0;
            observers[i].observe_seq = 0;
            observers[i].last_seen_ms = 0;
            observers[i].url[0] = 0;
            removed = true;
        }
    }
    return removed;
}

int Coap::notify(const char *url, const char *payload, int payload_len, COAP_CONTENT_TYPE type)
{
    if (url == NULL)
        return 0;
    unsigned long now = millis();
    int sent = 0;

    for (int i = 0; i < COAP_MAX_OBSERVERS; i++)
    {
        if (!observers[i].in_use)
            continue;
        if (!urlEquals(observers[i].url, url))
            continue;

        if (COAP_OBSERVER_LEASE_MS > 0 && (unsigned long)(now - observers[i].last_seen_ms) > COAP_OBSERVER_LEASE_MS)
        {
            observers[i].in_use = false;
            continue;
        }

        CoapPacket packet;
        packet.type = COAP_NONCON;
        packet.code = COAP_CONTENT;
        packet.token = observers[i].tokenlen ? observers[i].token : NULL;
        packet.tokenlen = observers[i].tokenlen;
        packet.payload = (uint8_t *)payload;
        packet.payloadlen = payload_len;
        packet.optionnum = 0;
        packet.messageid = rand();

        uint32_t observe_seq = ++observers[i].observe_seq;
        uint8_t observeBuf[3] = {0};
        uint8_t observeLen = encodeUintOption(observe_seq, observeBuf);
        packet.addOption(COAP_OBSERVE, observeLen, observeBuf);

        uint8_t optionBuffer[2] = {0};
        optionBuffer[0] = ((uint16_t)type & 0xFF00) >> 8;
        optionBuffer[1] = ((uint16_t)type & 0x00FF);
        packet.addOption(COAP_CONTENT_FORMAT, 2, optionBuffer);

        if (this->sendPacket(packet, observers[i].ip, observers[i].port) != 0)
            sent++;
    }
    return sent;
}
