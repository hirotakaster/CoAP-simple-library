param(
    [string] $LibraryPath = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$library = $LibraryPath
$header = Join-Path $library 'coap-simple.h'
$source = Join-Path $library 'coap-simple.cpp'

if (-not (Test-Path -LiteralPath $header) -or -not (Test-Path -LiteralPath $source)) {
    throw "Expected CoAP-simple library was not found at $library"
}

$h = (Get-Content -Raw -LiteralPath $header).Replace("`r`n", "`n")
$cpp = (Get-Content -Raw -LiteralPath $source).Replace("`r`n", "`n")

function Replace-ExactlyOnce {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Old,
        [Parameter(Mandatory)] [string] $New,
        [Parameter(Mandatory)] [string] $Label
    )
    $first = $Text.IndexOf($Old, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Missing expected block: $Label" }
    if ($Text.IndexOf($Old, $first + $Old.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Expected block is not unique: $Label"
    }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$old = @'
#ifndef COAP_MAX_OBSERVERS
#define COAP_MAX_OBSERVERS 4
#endif
'@
$new = @'
#ifndef COAP_MAX_OBSERVERS
#define COAP_MAX_OBSERVERS 4
#endif
#ifndef COAP_MAX_PENDING_CON
#define COAP_MAX_PENDING_CON 4
#endif
#ifndef COAP_ACK_TIMEOUT_MS
#define COAP_ACK_TIMEOUT_MS 750UL
#endif
#ifndef COAP_MAX_RETRANSMIT
#define COAP_MAX_RETRANSMIT 4
#endif
'@
$h = Replace-ExactlyOnce $h $old $new 'reliability configuration'

$old = @'
#else
typedef void (*CoapCallback)(CoapPacket &, IPAddress, int);
#endif
'@
$new = @'
#else
typedef void (*CoapCallback)(CoapPacket &, IPAddress, int);
#endif

typedef enum
{
    COAP_DELIVERY_ACKNOWLEDGED = 0,
    COAP_DELIVERY_TIMEOUT = 1,
    COAP_DELIVERY_RESET = 2
} COAP_DELIVERY_STATE;

#if defined(ESP8266) || defined(ESP32)
typedef std::function<void(uint16_t, IPAddress, int, COAP_DELIVERY_STATE)> CoapDeliveryCallback;
#else
typedef void (*CoapDeliveryCallback)(uint16_t, IPAddress, int, COAP_DELIVERY_STATE);
#endif
'@
$h = Replace-ExactlyOnce $h $old $new 'delivery callback type'

$old = @'
    uint8_t *tx_buffer = NULL;
    uint8_t *rx_buffer = NULL;

    struct ObserveEntry
'@
$new = @'
    uint8_t *tx_buffer = NULL;
    uint8_t *rx_buffer = NULL;
    CoapDeliveryCallback delivery = NULL;

    struct PendingCon
    {
        bool in_use = false;
        IPAddress ip;
        uint16_t port = 0;
        uint16_t messageid = 0;
        uint16_t packetlen = 0;
        uint8_t retransmits = 0;
        unsigned long next_retry_ms = 0;
        uint8_t *packet = NULL;
    };
    PendingCon pending[COAP_MAX_PENDING_CON];

    struct ObserveEntry
'@
$h = Replace-ExactlyOnce $h $old $new 'pending CON storage'

$old = @'
    uint16_t sendPacket(CoapPacket &packet, IPAddress ip);
    uint16_t sendPacket(CoapPacket &packet, IPAddress ip, int port);
    int parseOption(CoapOption *option, uint16_t *running_delta, uint8_t **buf, size_t buflen);
'@
$new = @'
    uint16_t sendPacket(CoapPacket &packet, IPAddress ip);
    uint16_t sendPacket(CoapPacket &packet, IPAddress ip, int port);
    bool sendRawPacket(const uint8_t *packet, size_t packetlen, IPAddress ip, int port);
    int findFreePending() const;
    int findPending(uint16_t messageid, IPAddress ip, int port) const;
    void clearPending(int index, COAP_DELIVERY_STATE state, bool notify);
    void processRetransmissions();
    int parseOption(CoapOption *option, uint16_t *running_delta, uint8_t **buf, size_t buflen);
'@
$h = Replace-ExactlyOnce $h $old $new 'private reliability methods'

$old = @'
    void response(CoapCallback c) { resp = c; }

    void server(CoapCallback c, String url) { uri.add(c, url); }
'@
$new = @'
    void response(CoapCallback c) { resp = c; }
    void onDelivery(CoapDeliveryCallback c) { delivery = c; }

    void server(CoapCallback c, String url) { uri.add(c, url); }
'@
$h = Replace-ExactlyOnce $h $old $new 'delivery callback registration'

$old = @'
    uint16_t put(IPAddress ip, int port, const char *url, const char *payload);
    uint16_t put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen);
'@
$new = @'
    uint16_t put(IPAddress ip, int port, const char *url, const char *payload);
    uint16_t put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen);
    uint16_t putNonConfirmable(IPAddress ip, int port, const char *url, const char *payload);
    uint16_t putNonConfirmable(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen);
'@
$h = Replace-ExactlyOnce $h $old $new 'NON PUT API'

$old = @'
    this->tx_buffer = new uint8_t[this->coap_buf_size];
    this->rx_buffer = new uint8_t[this->coap_buf_size];
}
'@
$new = @'
    this->tx_buffer = new uint8_t[this->coap_buf_size];
    this->rx_buffer = new uint8_t[this->coap_buf_size];
    for (int i = 0; i < COAP_MAX_PENDING_CON; i++)
        this->pending[i].packet = new uint8_t[this->coap_buf_size];
}
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'pending buffer allocation'

$old = @'
    if (this->rx_buffer != NULL)
        delete[] this->rx_buffer;
}
'@
$new = @'
    if (this->rx_buffer != NULL)
        delete[] this->rx_buffer;
    for (int i = 0; i < COAP_MAX_PENDING_CON; i++)
        if (this->pending[i].packet != NULL)
            delete[] this->pending[i].packet;
}
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'pending buffer cleanup'

$old = @'
uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip, int port)
{
    uint8_t *p = this->tx_buffer;
'@
$new = @'
uint16_t Coap::sendPacket(CoapPacket &packet, IPAddress ip, int port)
{
    int pendingIndex = -1;
    if (packet.type == COAP_CON)
    {
        pendingIndex = findFreePending();
        if (pendingIndex < 0)
            return 0;
    }

    uint8_t *p = this->tx_buffer;
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'reserve pending slot before CON send'

$old = @'
    _udp->beginPacket(ip, port);
    _udp->write(this->tx_buffer, packetSize);
    _udp->endPacket();

    return packet.messageid;
}
'@
$new = @'
    if (!sendRawPacket(this->tx_buffer, packetSize, ip, port))
        return 0;

    if (pendingIndex >= 0)
    {
        PendingCon &entry = pending[pendingIndex];
        entry.in_use = true;
        entry.ip = ip;
        entry.port = (uint16_t)port;
        entry.messageid = packet.messageid;
        entry.packetlen = packetSize;
        entry.retransmits = 0;
        entry.next_retry_ms = millis() + COAP_ACK_TIMEOUT_MS;
        memcpy(entry.packet, this->tx_buffer, packetSize);
    }

    return packet.messageid;
}

bool Coap::sendRawPacket(const uint8_t *packet, size_t packetlen, IPAddress ip, int port)
{
    if (packet == NULL || packetlen == 0 || packetlen >= (size_t)coap_buf_size)
        return false;
    if (_udp->beginPacket(ip, port) == 0)
        return false;
    if (_udp->write(packet, packetlen) != packetlen)
        return false;
    return _udp->endPacket() == 1;
}

int Coap::findFreePending() const
{
    for (int i = 0; i < COAP_MAX_PENDING_CON; i++)
        if (!pending[i].in_use && pending[i].packet != NULL)
            return i;
    return -1;
}

int Coap::findPending(uint16_t messageid, IPAddress ip, int port) const
{
    for (int i = 0; i < COAP_MAX_PENDING_CON; i++)
        if (pending[i].in_use && pending[i].messageid == messageid && pending[i].ip == ip && pending[i].port == (uint16_t)port)
            return i;
    return -1;
}

void Coap::clearPending(int index, COAP_DELIVERY_STATE state, bool notify)
{
    if (index < 0 || index >= COAP_MAX_PENDING_CON || !pending[index].in_use)
        return;
    uint16_t messageid = pending[index].messageid;
    IPAddress ip = pending[index].ip;
    int port = pending[index].port;
    pending[index].in_use = false;
    pending[index].packetlen = 0;
    if (notify && delivery)
        delivery(messageid, ip, port, state);
}

void Coap::processRetransmissions()
{
    unsigned long now = millis();
    for (int i = 0; i < COAP_MAX_PENDING_CON; i++)
    {
        PendingCon &entry = pending[i];
        if (!entry.in_use || (long)(now - entry.next_retry_ms) < 0)
            continue;
        if (entry.retransmits >= COAP_MAX_RETRANSMIT)
        {
            clearPending(i, COAP_DELIVERY_TIMEOUT, true);
            continue;
        }
        sendRawPacket(entry.packet, entry.packetlen, entry.ip, entry.port);
        entry.retransmits++;
        entry.next_retry_ms = now + (COAP_ACK_TIMEOUT_MS << entry.retransmits);
    }
}
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'send tracking and retransmission implementation'

$old = @'
uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen)
{
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, payloadlen);
}
'@
$new = @'
uint16_t Coap::put(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen)
{
    return this->send(ip, port, url, COAP_CON, COAP_PUT, NULL, 0, (uint8_t *)payload, payloadlen);
}

uint16_t Coap::putNonConfirmable(IPAddress ip, int port, const char *url, const char *payload)
{
    return this->send(ip, port, url, COAP_NONCON, COAP_PUT, NULL, 0, (uint8_t *)payload, strlen(payload));
}

uint16_t Coap::putNonConfirmable(IPAddress ip, int port, const char *url, const char *payload, size_t payloadlen)
{
    return this->send(ip, port, url, COAP_NONCON, COAP_PUT, NULL, 0, (uint8_t *)payload, payloadlen);
}
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'NON PUT implementation'

$old = @'
{
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, content_type, rand());
}
'@
$new = @'
{
    uint16_t messageid = 0;
    while (messageid == 0)
        messageid = (uint16_t)rand();
    return this->send(ip, port, url, type, method, token, tokenlen, payload, payloadlen, content_type, messageid);
}
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'nonzero message IDs'

$old = @'
bool Coap::loop()
{
    int32_t packetlen = _udp->parsePacket();
'@
$new = @'
bool Coap::loop()
{
    processRetransmissions();
    int32_t packetlen = _udp->parsePacket();
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'retransmission processing'

$old = @'
        if (packet.type == COAP_ACK)
        {
            // call response function
            resp(packet, _udp->remoteIP(), _udp->remotePort());
        }
        else
        {
'@
$new = @'
        if (packet.type == COAP_ACK || packet.type == COAP_RESET)
        {
            int pendingIndex = findPending(packet.messageid, _udp->remoteIP(), _udp->remotePort());
            if (pendingIndex >= 0)
                clearPending(pendingIndex, packet.type == COAP_ACK ? COAP_DELIVERY_ACKNOWLEDGED : COAP_DELIVERY_RESET, true);
            if (resp)
                resp(packet, _udp->remoteIP(), _udp->remotePort());
        }
        else
        {
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'ACK and RESET matching'

$old = @'
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
'@
$new = @'
            CoapCallback handler = uri.find(url);
            if (!handler)
            {
                if (packet.type == COAP_CON)
                    sendResponse(_udp->remoteIP(), _udp->remotePort(), packet.messageid, NULL, 0,
                                 COAP_NOT_FOUND, COAP_NONE, packet.token, packet.tokenlen);
            }
            else
            {
                // A transport ACK confirms receipt only. Application handlers still
                // send their own transaction-level result when processing matters.
                if (packet.type == COAP_CON)
                    sendResponse(_udp->remoteIP(), _udp->remotePort(), packet.messageid, NULL, 0,
                                 COAP_VALID, COAP_NONE, packet.token, packet.tokenlen);
                handler(packet, _udp->remoteIP(), _udp->remotePort());
            }
        }
'@
$cpp = Replace-ExactlyOnce $cpp $old $new 'automatic transport ACK for valid CON'

[System.IO.File]::WriteAllText($header, $h, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText($source, $cpp, [System.Text.UTF8Encoding]::new($false))

Write-Output "Repaired $header"
Write-Output "Repaired $source"
