// Integration tests that exercise the library the way the examples/ sketches do.
//
// Each example registers callbacks, calls coap.start(), and then in loop()
// reacts to incoming datagrams. These tests reproduce those exact callbacks
// (the LED "light" endpoint, the client response handler, and the Observe
// "subscribe" endpoint) and drive a full request -> callback -> response
// round-trip through Coap::loop() using the FakeUDP transport.
#include "coap-simple.h"
#include "FakeUDP.h"
#include "CoapCodec.h"
#include "TestHarness.h"

#include <cstring>
#include <string>
#include <vector>

// In the example sketches `coap` is a global object that the (function-pointer)
// callbacks reach for. We mirror that with a global pointer set per test.
static Coap *g_coap = nullptr;

// CoAP option numbers used in assertions below.
static const uint16_t OPT_OBSERVE = 6;
static const uint16_t OPT_URI_PATH = 11;
static const uint16_t OPT_CONTENT_FORMAT = 12;
static const uint16_t OPT_URI_QUERY = 15;

// --- examples/coapserver, esp32, esp8266: "light" endpoint + response handler ---

// callback_light(), as in examples/coapserver/coapserver.ino.
static bool g_ledState = true;

static void callback_light(CoapPacket &packet, IPAddress ip, int port)
{
    char p[COAP_BUF_MAX_SIZE + 1];
    memcpy(p, packet.payload, packet.payloadlen);
    p[packet.payloadlen] = '\0';

    String message(p);

    if (message.equals("0"))
        g_ledState = false;
    else if (message.equals("1"))
        g_ledState = true;

    if (g_ledState)
        g_coap->sendResponse(ip, port, packet.messageid, "1");
    else
        g_coap->sendResponse(ip, port, packet.messageid, "0");
}

// callback_response(): the example just reads and prints the payload.
static std::string g_lastResponse;
static int g_responseHits = 0;
static void callback_response(CoapPacket &packet, IPAddress, int)
{
    char p[COAP_BUF_MAX_SIZE + 1];
    memcpy(p, packet.payload, packet.payloadlen);
    p[packet.payloadlen] = '\0';
    g_lastResponse = p;
    g_responseHits++;
}

// A remote coap-client doing `put coap://device/light -e "1"` should flip the
// LED on and get "1" back; "0" flips it off and returns "0".
static void test_example_light_put_toggles_led_and_responds()
{
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;
    g_ledState = true;

    coap.server(callback_light, "light");
    coap.start();

    IPAddress client(192, 168, 0, 50);

    // PUT "1"
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_PUT, 0x0101, {"light"}, {}, "1"),
                     client, 40000);
    coap.loop();
    CHECK(g_ledState == true);
    CHECK_EQ(udp.sent.size(), 1u);
    {
        DecodedPacket d = coapDecode(udp.sent[0].data);
        CHECK(d.valid);
        CHECK_EQ(d.type, (uint8_t)COAP_ACK);
        CHECK_EQ(d.code, (uint8_t)COAP_CONTENT);
        CHECK_EQ(d.messageid, 0x0101);
        CHECK_EQ(d.payloadStr(), "1");
        CHECK(udp.sent[0].ip == client);
        CHECK_EQ(udp.sent[0].port, 40000);
    }

    // PUT "0"
    udp.reset();
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_PUT, 0x0102, {"light"}, {}, "0"),
                     client, 40000);
    coap.loop();
    CHECK(g_ledState == false);
    {
        DecodedPacket d = coapDecode(udp.sent[0].data);
        CHECK_EQ(d.payloadStr(), "0");
        CHECK_EQ(d.messageid, 0x0102);
    }
}

// A GET on /light (no payload) should report the current LED state.
static void test_example_light_get_reports_state()
{
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;
    g_ledState = true;

    coap.server(callback_light, "light");
    coap.start();

    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 0x0200, {"light"}),
                     IPAddress(10, 0, 0, 9), 5683);
    coap.loop();

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.payloadStr(), "1");
}

// examples/coaptest: the client sends a GET, then the server's ACK comes back
// and the registered response callback receives the payload.
static void test_example_client_get_then_response_callback()
{
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;
    g_lastResponse = "";
    g_responseHits = 0;

    coap.response(callback_response);
    coap.start();

    // Client side: coap.get(ip, 5683, "time")
    uint16_t mid = coap.get(IPAddress(10, 0, 0, 1), 5683, "time");
    CHECK_EQ(udp.sent.size(), 1u);
    DecodedPacket req = coapDecode(udp.sent[0].data);
    CHECK_EQ(req.code, (uint8_t)COAP_GET);
    CHECK_EQ(req.optionString(OPT_URI_PATH), "time");

    // Server replies with an ACK 2.05 carrying the time; loop() routes ACKs to
    // the response callback.
    udp.pushIncoming(coapBuildAck((uint8_t)COAP_CONTENT, mid, "12:00"),
                     IPAddress(10, 0, 0, 1), 5683);
    coap.loop();

    CHECK_EQ(g_responseHits, 1);
    CHECK_EQ(g_lastResponse, "12:00");
}

// The example comments document registering several endpoints, including
// nested ones like "env/temp". Verify multi-endpoint dispatch works.
static int g_tempHits = 0;
static void callback_temp(CoapPacket &packet, IPAddress ip, int port)
{
    g_tempHits++;
    g_coap->sendResponse(ip, port, packet.messageid, "23");
}
static void test_example_multiple_endpoints()
{
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;
    g_ledState = true;
    g_tempHits = 0;

    coap.server(callback_light, "light");
    coap.server(callback_temp, "env/temp");
    coap.start();

    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 1, {"env", "temp"}),
                     IPAddress(1, 2, 3, 4), 5683);
    coap.loop();

    CHECK_EQ(g_tempHits, 1);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK_EQ(d.payloadStr(), "23");
}

// A realistic server callback that inspects the parsed request with the
// CoapPacket accessors (getUriPath / getQuery / getContentFormat).
static std::string g_seenPath;
static std::string g_seenQuery;
static int g_seenFormat;
static void callback_introspect(CoapPacket &packet, IPAddress ip, int port)
{
    char path[64];
    char query[64];
    packet.getUriPath(path, sizeof(path));
    packet.getQuery(query, sizeof(query));
    g_seenPath = path;
    g_seenQuery = query;
    g_seenFormat = packet.getContentFormat();
    g_coap->sendResponse(ip, port, packet.messageid, "ok");
}

static void test_example_callback_reads_request()
{
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;
    g_seenPath = g_seenQuery = "";
    g_seenFormat = 0;

    coap.server(callback_introspect, "api/data");
    coap.start();

    // GET api/data?x=1 with Content-Format application/json. Options must be in
    // ascending order: Uri-Path(11), Content-Format(12), Uri-Query(15).
    std::vector<uint8_t> buf = {
        (uint8_t)((0x01 << 6) | ((COAP_CON & 0x03) << 4) | 0),
        (uint8_t)COAP_GET, 0x00, 0x05};
    uint16_t running = 0;
    coapAppendOption(buf, OPT_URI_PATH, running, {'a', 'p', 'i'});
    coapAppendOption(buf, OPT_URI_PATH, running, {'d', 'a', 't', 'a'});
    coapAppendOption(buf, OPT_CONTENT_FORMAT, running, {0x00, 0x32}); // 50
    coapAppendOption(buf, OPT_URI_QUERY, running, {'x', '=', '1'});

    udp.pushIncoming(buf, IPAddress(5, 6, 7, 8), 5683);
    coap.loop();

    CHECK_EQ(g_seenPath, "api/data");
    CHECK_EQ(g_seenQuery, "x=1");
    CHECK_EQ(g_seenFormat, (int)COAP_APPLICATION_JSON);
    CHECK_EQ(coapDecode(udp.sent[0].data).payloadStr(), "ok");
}

// --- examples/coapserver-with-observe: the "subscribe" Observe endpoint ---

// endpoint_subscribe(), as in the coapserver-with-observe sketch.
static void endpoint_subscribe(CoapPacket &packet, IPAddress ip, int port)
{
    uint32_t observeValue = 0;
    if (!packet.getObserveValue(observeValue))
    {
        // No (or invalid) Observe option: treat as a normal GET.
        char payload[6];
        snprintf(payload, sizeof(payload), "%u", 42);
        int payload_len = strlen(payload);
        g_coap->sendResponse(ip, port, packet.messageid, payload, payload_len,
                             COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
        return;
    }

    if (observeValue == 0)
    {
        if (!g_coap->addObserver("subscribe", ip, port, packet.token, packet.tokenlen))
        {
            g_coap->sendResponse(ip, port, packet.messageid, "busy", strlen("busy"),
                                 COAP_SERVICE_UNAVAILABLE, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
            return;
        }
        g_coap->sendObserveResponse(ip, port, packet.messageid, "subscribed", strlen("subscribed"),
                                    COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen, 0);
    }
    else if (observeValue == 1)
    {
        g_coap->removeObserver("subscribe", ip, port, packet.token, packet.tokenlen);
        g_coap->sendResponse(ip, port, packet.messageid, "unsubscribed", strlen("unsubscribed"),
                             COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
    }
    else
    {
        g_coap->sendResponse(ip, port, packet.messageid, "invalid", strlen("invalid"),
                             COAP_BAD_OPTION, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
    }
}

// A plain GET (no Observe option) returns the current value and echoes token.
static void test_example_subscribe_plain_get()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;

    coap.server(endpoint_subscribe, "subscribe");
    coap.start();

    std::vector<uint8_t> token = {0xA1, 0xB2};
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 0x3001, {"subscribe"}, token),
                     IPAddress(7, 7, 7, 7), 5683);
    coap.loop();

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_ACK);
    CHECK_EQ(d.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(d.payloadStr(), "42");
    CHECK_EQ(d.token, token); // token echoed
    CHECK_EQ(d.optionCount(OPT_OBSERVE), 0); // plain GET, no observe option
}

// Observe=0 registers the observer and replies with an Observe response, after
// which notify() reaches that observer with the right token and sequence.
static void test_example_subscribe_register_and_notify()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;

    coap.server(endpoint_subscribe, "subscribe");
    coap.start();

    IPAddress observer(7, 7, 7, 8);
    std::vector<uint8_t> token = {0xC0, 0xDE};

    // Subscribe (Observe option value 0).
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 0x3100, {"subscribe"}, token,
                                      "", /*observe=*/true, /*observeValue=*/0),
                     observer, 41000);
    coap.loop();

    CHECK_EQ(udp.sent.size(), 1u);
    DecodedPacket reg = coapDecode(udp.sent[0].data);
    CHECK(reg.valid);
    CHECK_EQ(reg.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(reg.payloadStr(), "subscribed");
    CHECK(reg.option(OPT_OBSERVE) != nullptr);                 // observe response
    CHECK_EQ(reg.option(OPT_OBSERVE)->value.size(), 0u);       // seq 0 -> empty
    CHECK_EQ(reg.token, token);

    // Now the application pushes a notification to all observers of "subscribe".
    udp.reset();
    int sent = coap.notify("subscribe", "42", 2, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 1);
    CHECK_EQ(udp.sent.size(), 1u);

    DecodedPacket note = coapDecode(udp.sent[0].data);
    CHECK(note.valid);
    CHECK_EQ(note.type, (uint8_t)COAP_NONCON);   // notifications are non-confirmable
    CHECK_EQ(note.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(note.token, token);                 // delivered with subscriber's token
    CHECK_EQ(note.payloadStr(), "42");
    CHECK(note.option(OPT_OBSERVE)->value == std::vector<uint8_t>({1})); // first seq
    CHECK(udp.sent[0].ip == observer);
    CHECK_EQ(udp.sent[0].port, 41000);
}

// Observe=1 deregisters: the endpoint replies "unsubscribed" and subsequent
// notify() reaches nobody.
static void test_example_subscribe_then_unsubscribe()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;

    coap.server(endpoint_subscribe, "subscribe");
    coap.start();

    IPAddress observer(7, 7, 7, 9);
    std::vector<uint8_t> token = {0x01};

    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 1, {"subscribe"}, token, "", true, 0),
                     observer, 42000);
    coap.loop();
    CHECK_EQ(coap.notify("subscribe", "x", 1, COAP_TEXT_PLAIN), 1);

    // Unsubscribe (Observe option value 1).
    udp.reset();
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 2, {"subscribe"}, token, "", true, 1),
                     observer, 42000);
    coap.loop();

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK_EQ(d.payloadStr(), "unsubscribed");

    // No observers left.
    CHECK_EQ(coap.notify("subscribe", "x", 1, COAP_TEXT_PLAIN), 0);
}

// An out-of-range Observe value yields a 4.02 Bad Option, per the example.
static void test_example_subscribe_invalid_observe_value()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    g_coap = &coap;

    coap.server(endpoint_subscribe, "subscribe");
    coap.start();

    std::vector<uint8_t> token = {0x55};
    udp.pushIncoming(coapBuildRequest(COAP_CON, COAP_GET, 9, {"subscribe"}, token, "", true, 2),
                     IPAddress(7, 7, 7, 10), 5683);
    coap.loop();

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK_EQ(d.code, (uint8_t)COAP_BAD_OPTION);
    CHECK_EQ(d.payloadStr(), "invalid");
}

void runExampleTests()
{
    RUN(test_example_light_put_toggles_led_and_responds);
    RUN(test_example_light_get_reports_state);
    RUN(test_example_client_get_then_response_callback);
    RUN(test_example_multiple_endpoints);
    RUN(test_example_callback_reads_request);
    RUN(test_example_subscribe_plain_get);
    RUN(test_example_subscribe_register_and_notify);
    RUN(test_example_subscribe_then_unsubscribe);
    RUN(test_example_subscribe_invalid_observe_value);
}
