// Host-side unit tests for the coap-simple library.
//
// These tests compile the real library against a mocked Arduino environment
// (see test/mocks) and a FakeUDP transport that captures outgoing datagrams
// and injects incoming ones. No hardware is required.
#include "coap-simple.h"
#include "FakeUDP.h"
#include "CoapCodec.h"
#include "TestHarness.h"

#include <cstdio>
#include <vector>
#include <string>

// Definition of the test-controlled millis() clock declared in Arduino.h.
unsigned long __mock_millis_value = 0;

// Definitions of the shared harness counters declared in TestHarness.h.
int g_checks = 0;
int g_failures = 0;
const char *g_current_test = "";

// Example-driven integration tests live in test_examples.cpp.
void runExampleTests();

// Server/response callbacks are plain function pointers on this build, so they
// report back to the tests through these globals.
struct CallbackCapture
{
    int hits = 0;
    IPAddress ip;
    int port = 0;
    uint8_t code = 0;
    uint8_t type = 0;
    std::string payload;
    bool isObserve = false;
    uint32_t observeValue = 0;
    bool observeValid = false;
};

static CallbackCapture g_serverCb;
static CallbackCapture g_serverCb2;
static CallbackCapture g_respCb;

static void recordInto(CallbackCapture &c, CoapPacket &packet, IPAddress ip, int port)
{
    c.hits++;
    c.ip = ip;
    c.port = port;
    c.code = packet.code;
    c.type = packet.type;
    c.payload.assign((const char *)packet.payload, packet.payloadlen);
    c.isObserve = packet.isObserve();
    c.observeValid = packet.getObserveValue(c.observeValue);
}

static void serverCallback(CoapPacket &packet, IPAddress ip, int port)
{
    recordInto(g_serverCb, packet, ip, port);
}
static void serverCallback2(CoapPacket &packet, IPAddress ip, int port)
{
    recordInto(g_serverCb2, packet, ip, port);
}
static void responseCallback(CoapPacket &packet, IPAddress ip, int port)
{
    recordInto(g_respCb, packet, ip, port);
}

static void resetCaptures()
{
    g_serverCb = CallbackCapture();
    g_serverCb2 = CallbackCapture();
    g_respCb = CallbackCapture();
}

// Option numbers (mirrors COAP_OPTION_NUMBER without depending on enum scope).
static const uint16_t OPT_URI_HOST = 3;
static const uint16_t OPT_OBSERVE = 6;
static const uint16_t OPT_URI_PATH = 11;
static const uint16_t OPT_CONTENT_FORMAT = 12;
static const uint16_t OPT_URI_QUERY = 15;

// --- CoapPacket ---

static void test_addOption_stores_fields()
{
    CoapPacket p;
    uint8_t buf[3] = {1, 2, 3};
    p.addOption(OPT_URI_PATH, 3, buf);
    CHECK_EQ(p.optionnum, 1);
    CHECK_EQ(p.options[0].number, OPT_URI_PATH);
    CHECK_EQ(p.options[0].length, 3);
    CHECK_EQ(p.options[0].buffer[2], 3);
}

static void test_addOption_respects_max()
{
    CoapPacket p;
    uint8_t b = 0;
    for (int i = 0; i < COAP_MAX_OPTION_NUM + 5; i++)
        p.addOption(OPT_URI_PATH, 1, &b);
    CHECK_EQ(p.optionnum, COAP_MAX_OPTION_NUM);
}

static void test_isObserve()
{
    CoapPacket p;
    uint8_t b = 0;
    CHECK(!p.isObserve());
    p.addOption(OPT_CONTENT_FORMAT, 1, &b);
    CHECK(!p.isObserve());
    p.addOption(OPT_OBSERVE, 0, &b);
    CHECK(p.isObserve());
}

static void test_getObserveValue()
{
    CoapPacket p;
    uint32_t v = 123;

    // No observe option -> false, value untouched semantics not relied upon.
    CHECK(!p.getObserveValue(v));

    uint8_t empty = 0;
    p.addOption(OPT_OBSERVE, 0, &empty); // zero-length -> value 0
    v = 999;
    CHECK(p.getObserveValue(v));
    CHECK_EQ(v, 0);

    CoapPacket p2;
    uint8_t three[3] = {0x01, 0x02, 0x03};
    p2.addOption(OPT_OBSERVE, 3, three);
    CHECK(p2.getObserveValue(v));
    CHECK_EQ(v, 0x010203u);

    // length > 3 is invalid for the observe option.
    CoapPacket p3;
    uint8_t four[4] = {1, 2, 3, 4};
    p3.addOption(OPT_OBSERVE, 4, four);
    CHECK(!p3.getObserveValue(v));
}

static void test_getOption()
{
    CoapPacket p;
    uint8_t path[3] = {'a', 'p', 'i'};
    uint8_t cf[2] = {0x00, 0x32};
    p.addOption(OPT_URI_PATH, 3, path);
    p.addOption(OPT_CONTENT_FORMAT, 2, cf);

    CoapOption *o = p.getOption(OPT_CONTENT_FORMAT);
    CHECK(o != NULL);
    CHECK_EQ(o->length, 2);
    CHECK(p.getOption(OPT_URI_QUERY) == NULL);
}

static void test_getContentFormat()
{
    CoapPacket p;
    CHECK_EQ(p.getContentFormat(), -1);

    uint8_t cf[2] = {0x00, 0x32}; // 50 -> application/json
    p.addOption(OPT_CONTENT_FORMAT, 2, cf);
    CHECK_EQ(p.getContentFormat(), (int)COAP_APPLICATION_JSON);
}

static void test_getUriPath()
{
    CoapPacket p;
    char buf[64];

    // No path options -> empty string, length 0.
    CHECK_EQ(p.getUriPath(buf, sizeof(buf)), 0);
    CHECK_EQ(std::string(buf), "");

    uint8_t a[3] = {'a', 'p', 'i'};
    uint8_t b[4] = {'t', 'e', 'm', 'p'};
    p.addOption(OPT_URI_PATH, 3, a);
    p.addOption(OPT_URI_PATH, 4, b);
    CHECK_EQ(p.getUriPath(buf, sizeof(buf)), 8);
    CHECK_EQ(std::string(buf), "api/temp");

    char small[4];
    CHECK_EQ(p.getUriPath(small, sizeof(small)), -1); // doesn't fit
}

static void test_getQuery()
{
    CoapPacket p;
    char buf[64];
    uint8_t q1[3] = {'a', '=', '1'};
    uint8_t q2[3] = {'b', '=', '2'};
    p.addOption(OPT_URI_QUERY, 3, q1);
    p.addOption(OPT_URI_QUERY, 3, q2);
    CHECK_EQ(p.getQuery(buf, sizeof(buf)), 7);
    CHECK_EQ(std::string(buf), "a=1&b=2");
}

// --- Client requests: get / put / send ---

static void test_get_builds_con_get()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();
    CHECK(udp.begin_called);
    CHECK_EQ(udp.begin_port, COAP_DEFAULT_PORT);

    uint16_t mid = coap.get(IPAddress(192, 168, 1, 1), 5683, "hello");
    CHECK_EQ(udp.sent.size(), 1u);

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.version, 1);
    CHECK_EQ(d.type, (uint8_t)COAP_CON);
    CHECK_EQ(d.code, (uint8_t)COAP_GET);
    CHECK_EQ(d.messageid, mid);
    CHECK_EQ(d.tokenlen, 0);
    CHECK_EQ(d.optionString(OPT_URI_HOST), "192.168.1.1");
    CHECK_EQ(d.optionString(OPT_URI_PATH), "hello");
    CHECK_EQ(d.optionCount(OPT_CONTENT_FORMAT), 0); // COAP_NONE -> no content-format
    CHECK_EQ(d.payload.size(), 0u);
    CHECK_EQ(udp.sent[0].port, 5683);
}

static void test_put_builds_payload()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.put(IPAddress(10, 0, 0, 5), 5683, "res", "value");
    CHECK_EQ(udp.sent.size(), 1u);

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_CON);
    CHECK_EQ(d.code, (uint8_t)COAP_PUT);
    CHECK_EQ(d.optionString(OPT_URI_PATH), "res");
    CHECK_EQ(d.payloadStr(), "value");
}

static void test_post_builds_payload()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.post(IPAddress(10, 0, 0, 5), 5683, "res", "value");
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.code, (uint8_t)COAP_POST);
    CHECK_EQ(d.optionString(OPT_URI_PATH), "res");
    CHECK_EQ(d.payloadStr(), "value");
}

static void test_del_builds_delete()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.del(IPAddress(10, 0, 0, 5), 5683, "res");
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.code, (uint8_t)COAP_DELETE);
    CHECK_EQ(d.optionString(OPT_URI_PATH), "res");
    CHECK_EQ(d.payload.size(), 0u);
}

static void test_send_with_content_format()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.send(IPAddress(1, 2, 3, 4), 5683, "x", COAP_CON, COAP_POST, NULL, 0,
              (const uint8_t *)"hi", 2, COAP_APPLICATION_JSON, 0x1234);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.messageid, 0x1234);
    CHECK_EQ(d.code, (uint8_t)COAP_POST);
    const DecodedOption *cf = d.option(OPT_CONTENT_FORMAT);
    CHECK(cf != nullptr);
    CHECK_EQ(cf->value.size(), 2u);
    uint16_t cfval = (cf->value[0] << 8) | cf->value[1];
    CHECK_EQ(cfval, (uint16_t)COAP_APPLICATION_JSON);
}

static void test_send_with_token()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    coap.send(IPAddress(1, 2, 3, 4), 5683, "a", COAP_CON, COAP_GET, token, 4,
              NULL, 0, COAP_NONE, 0x4321);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.tokenlen, 4);
    CHECK_EQ(d.token.size(), 4u);
    CHECK(d.token == std::vector<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}));
}

static void test_query_parsing()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.send(IPAddress(1, 2, 3, 4), 5683, "path?x=1&y=2", COAP_CON, COAP_GET,
              NULL, 0, NULL, 0, COAP_NONE, 1);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.optionString(OPT_URI_PATH), "path");
    CHECK_EQ(d.optionCount(OPT_URI_QUERY), 2);
    CHECK_EQ(d.options[2].str(),
             "x=1");
    CHECK_EQ(d.options[3].str(),
             "y=2");
}

static void test_multi_segment_path()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.get(IPAddress(1, 2, 3, 4), 5683, "a/b/c");
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.optionCount(OPT_URI_PATH), 3);
    CHECK_EQ(d.options[1].str(), "a");
    CHECK_EQ(d.options[2].str(), "b");
    CHECK_EQ(d.options[3].str(), "c");
}

// --- Empty messages: ping / reset ---

static void test_ping_is_empty_con()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint16_t mid = coap.ping(IPAddress(1, 2, 3, 4), 5683);
    CHECK_EQ(udp.sent.size(), 1u);
    CHECK_EQ(udp.sent[0].data.size(), 4u); // header only

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_CON);
    CHECK_EQ(d.code, 0);
    CHECK_EQ(d.tokenlen, 0);
    CHECK_EQ(d.messageid, mid);
    CHECK_EQ(d.options.size(), 0u);
    CHECK_EQ(d.payload.size(), 0u);
}

static void test_sendReset_is_empty_rst()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.sendReset(IPAddress(1, 2, 3, 4), 5683, 0xBEEF);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(udp.sent[0].data.size(), 4u);
    CHECK_EQ(d.type, (uint8_t)COAP_RESET);
    CHECK_EQ(d.code, 0);
    CHECK_EQ(d.messageid, 0xBEEF);
}

// --- Responses ---

static void test_sendResponse_basic()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.sendResponse(IPAddress(1, 2, 3, 4), 5683, 0xABCD, "ok");
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_ACK);
    CHECK_EQ(d.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(d.messageid, 0xABCD);
    CHECK_EQ(d.payloadStr(), "ok");
    CHECK(d.option(OPT_CONTENT_FORMAT) != nullptr);
}

static void test_sendObserveResponse_includes_observe()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[2] = {0xAA, 0xBB};
    coap.sendObserveResponse(IPAddress(1, 2, 3, 4), 5683, 0x1111, "data", 4,
                             COAP_CONTENT, COAP_TEXT_PLAIN, token, 2, 300);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_ACK);
    CHECK_EQ(d.tokenlen, 2);
    const DecodedOption *obs = d.option(OPT_OBSERVE);
    CHECK(obs != nullptr);
    // 300 needs two bytes: 0x01 0x2C
    CHECK_EQ(obs->value.size(), 2u);
    uint32_t val = 0;
    for (uint8_t b : obs->value)
        val = (val << 8) | b;
    CHECK_EQ(val, 300u);
}

static void test_observe_uint_encoding_lengths()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    struct
    {
        uint32_t seq;
        size_t len;
    } cases[] = {{0, 0}, {1, 1}, {255, 1}, {256, 2}, {65535, 2}, {65536, 3}};

    for (auto &c : cases)
    {
        udp.reset();
        coap.sendObserveResponse(IPAddress(1, 2, 3, 4), 5683, 1, NULL, 0,
                                 COAP_CONTENT, COAP_TEXT_PLAIN, NULL, 0, c.seq);
        DecodedPacket d = coapDecode(udp.sent[0].data);
        const DecodedOption *obs = d.option(OPT_OBSERVE);
        CHECK(obs != nullptr);
        CHECK_EQ(obs->value.size(), c.len);
    }
}

// --- loop() dispatch ---

static void test_loop_dispatches_to_server()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback, "hello");
    coap.start();

    auto pkt = coapBuildRequest(COAP_NONCON, COAP_GET, 0x2020, {"hello"});
    udp.pushIncoming(pkt, IPAddress(9, 8, 7, 6), 1234);
    coap.loop();

    CHECK_EQ(g_serverCb.hits, 1);
    CHECK_EQ(g_serverCb.code, (uint8_t)COAP_GET);
    CHECK_EQ(g_serverCb.port, 1234);
    CHECK(g_serverCb.ip == IPAddress(9, 8, 7, 6));
}

static void test_loop_multi_segment_url()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback2, "a/b");
    coap.start();

    auto pkt = coapBuildRequest(COAP_NONCON, COAP_GET, 1, {"a", "b"});
    udp.pushIncoming(pkt, IPAddress(1, 1, 1, 1), 5683);
    coap.loop();

    CHECK_EQ(g_serverCb2.hits, 1);
}

static void test_loop_unknown_url_sends_not_found()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback, "known");
    coap.start();

    auto pkt = coapBuildRequest(COAP_NONCON, COAP_GET, 0x7777, {"missing"});
    udp.pushIncoming(pkt, IPAddress(2, 2, 2, 2), 5683);
    coap.loop();

    CHECK_EQ(g_serverCb.hits, 0);
    CHECK_EQ(udp.sent.size(), 1u);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.code, (uint8_t)COAP_NOT_FOUND);
    CHECK_EQ(d.messageid, 0x7777);
}

static void test_loop_ack_calls_response()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.response(responseCallback);
    coap.start();

    udp.pushIncoming(coapBuildAck((uint8_t)COAP_CONTENT, 0x1234, "pong"),
                     IPAddress(5, 5, 5, 5), 5683);
    coap.loop();

    CHECK_EQ(g_respCb.hits, 1);
    CHECK_EQ(g_respCb.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(g_respCb.payload, "pong");
}

static void test_loop_observe_request_flags()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback, "temp");
    coap.start();

    auto pkt = coapBuildRequest(COAP_NONCON, COAP_GET, 1, {"temp"}, {},
                                "", /*observe=*/true, /*observeValue=*/0);
    udp.pushIncoming(pkt, IPAddress(3, 3, 3, 3), 5683);
    coap.loop();

    CHECK_EQ(g_serverCb.hits, 1);
    CHECK(g_serverCb.isObserve);
    CHECK(g_serverCb.observeValid);
    CHECK_EQ(g_serverCb.observeValue, 0u);
}

static void test_loop_ignores_malformed()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback, "hello");
    coap.start();

    // Too short / wrong version.
    udp.pushIncoming({0x00, 0x01}, IPAddress(1, 1, 1, 1), 5683);
    coap.loop();
    CHECK_EQ(g_serverCb.hits, 0);
    CHECK_EQ(udp.sent.size(), 0u);
}

static void test_loop_responds_to_ping_with_reset()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.server(serverCallback, "hello");
    coap.start();

    // An empty Confirmable message is a CoAP ping.
    udp.pushIncoming(coapBuildRequest(COAP_CON, 0, 0x4242, {}), IPAddress(9, 9, 9, 9), 7000);
    coap.loop();

    CHECK_EQ(g_serverCb.hits, 0); // not dispatched as a request
    CHECK_EQ(udp.sent.size(), 1u);
    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK_EQ(d.type, (uint8_t)COAP_RESET);
    CHECK_EQ(d.code, 0);
    CHECK_EQ(d.messageid, 0x4242);
    CHECK(udp.sent[0].ip == IPAddress(9, 9, 9, 9));
    CHECK_EQ(udp.sent[0].port, 7000);
}

static void test_loop_ignores_empty_reset()
{
    resetCaptures();
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    // A peer's Reset (e.g. the reply to our ping) is silently ignored.
    udp.pushIncoming(coapBuildRequest(COAP_RESET, 0, 0x0001, {}), IPAddress(1, 1, 1, 1), 5683);
    coap.loop();
    CHECK_EQ(udp.sent.size(), 0u);
}

// --- Observers and notify ---

static void test_addObserver_and_notify()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[2] = {0x01, 0x02};
    CHECK(coap.addObserver("sensor", IPAddress(4, 4, 4, 4), 5683, token, 2));

    int sent = coap.notify("sensor", "21C", 3, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 1);
    CHECK_EQ(udp.sent.size(), 1u);

    DecodedPacket d = coapDecode(udp.sent[0].data);
    CHECK(d.valid);
    CHECK_EQ(d.type, (uint8_t)COAP_NONCON);
    CHECK_EQ(d.code, (uint8_t)COAP_CONTENT);
    CHECK_EQ(d.tokenlen, 2);
    CHECK_EQ(d.payloadStr(), "21C");
    const DecodedOption *obs = d.option(OPT_OBSERVE);
    CHECK(obs != nullptr);
    CHECK(obs->value == std::vector<uint8_t>({1})); // first notification -> seq 1
}

static void test_notify_increments_sequence()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.addObserver("s", IPAddress(4, 4, 4, 4), 5683, NULL, 0);
    coap.notify("s", "a", 1, COAP_TEXT_PLAIN);
    coap.notify("s", "b", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(udp.sent.size(), 2u);

    DecodedPacket d1 = coapDecode(udp.sent[0].data);
    DecodedPacket d2 = coapDecode(udp.sent[1].data);
    CHECK(d1.option(OPT_OBSERVE)->value == std::vector<uint8_t>({1}));
    CHECK(d2.option(OPT_OBSERVE)->value == std::vector<uint8_t>({2}));
}

static void test_addObserver_dedup()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[1] = {0x09};
    CHECK(coap.addObserver("d", IPAddress(4, 4, 4, 4), 5683, token, 1));
    CHECK(coap.addObserver("d", IPAddress(4, 4, 4, 4), 5683, token, 1)); // duplicate

    int sent = coap.notify("d", "x", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 1); // only one observer registered
}

static void test_addObserver_capacity()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    for (int i = 0; i < COAP_MAX_OBSERVERS; i++)
    {
        uint8_t token[1] = {(uint8_t)i};
        CHECK(coap.addObserver("c", IPAddress(4, 4, 4, 4), 5683, token, 1));
    }
    uint8_t extra[1] = {0xFF};
    CHECK(!coap.addObserver("c", IPAddress(4, 4, 4, 4), 5683, extra, 1));

    int sent = coap.notify("c", "x", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, COAP_MAX_OBSERVERS);
}

static void test_removeObserver()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[1] = {0x07};
    coap.addObserver("r", IPAddress(4, 4, 4, 4), 5683, token, 1);
    CHECK(coap.removeObserver("r", IPAddress(4, 4, 4, 4), 5683, token, 1));

    int sent = coap.notify("r", "x", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 0);

    // Removing again returns false.
    CHECK(!coap.removeObserver("r", IPAddress(4, 4, 4, 4), 5683, token, 1));
}

static void test_addObserver_rejects_invalid()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    // URL too long.
    std::string longUrl(COAP_MAX_OBSERVE_URL_LEN + 1, 'a');
    CHECK(!coap.addObserver(longUrl.c_str(), IPAddress(4, 4, 4, 4), 5683, NULL, 0));

    // Token too long.
    uint8_t token[9] = {0};
    CHECK(!coap.addObserver("ok", IPAddress(4, 4, 4, 4), 5683, token, 9));

    // Null URL.
    CHECK(!coap.addObserver(NULL, IPAddress(4, 4, 4, 4), 5683, NULL, 0));
}

static void test_notify_lease_expiry()
{
    setMillis(1000);
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    coap.addObserver("lease", IPAddress(4, 4, 4, 4), 5683, NULL, 0);

    // Advance the clock beyond the lease window.
    setMillis(1000 + COAP_OBSERVER_LEASE_MS + 1);
    int sent = coap.notify("lease", "x", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 0);
    CHECK_EQ(udp.sent.size(), 0u);

    // Observer should now be evicted, so a subsequent notify also sends nothing.
    sent = coap.notify("lease", "x", 1, COAP_TEXT_PLAIN);
    CHECK_EQ(sent, 0);
}

static void test_notify_observer_object()
{
    FakeUDP udp;
    Coap coap(udp);
    coap.start();

    uint8_t token[3] = {0x11, 0x22, 0x33};
    Observer obs(IPAddress(8, 8, 8, 8), 5683, token, 3);
    coap.notify(&obs, "hi", 2, COAP_TEXT_PLAIN);
    coap.notify(&obs, "hi", 2, COAP_TEXT_PLAIN);
    CHECK_EQ(udp.sent.size(), 2u);

    DecodedPacket d1 = coapDecode(udp.sent[0].data);
    CHECK_EQ(d1.type, (uint8_t)COAP_NONCON);
    CHECK_EQ(d1.tokenlen, 3);
    CHECK(d1.option(OPT_OBSERVE)->value == std::vector<uint8_t>({1}));

    DecodedPacket d2 = coapDecode(udp.sent[1].data);
    CHECK(d2.option(OPT_OBSERVE)->value == std::vector<uint8_t>({2}));
}

static void test_observer_token_clamped()
{
    uint8_t token[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    Observer obs(IPAddress(1, 1, 1, 1), 5683, token, 10);
    CHECK_EQ(obs.token_len, 8);
}

int main()
{
    printf("Running coap-simple unit tests\n\n");
    printf("== Library unit tests ==\n");

    // CoapPacket
    RUN(test_addOption_stores_fields);
    RUN(test_addOption_respects_max);
    RUN(test_isObserve);
    RUN(test_getObserveValue);
    RUN(test_getOption);
    RUN(test_getContentFormat);
    RUN(test_getUriPath);
    RUN(test_getQuery);

    // Client requests
    RUN(test_get_builds_con_get);
    RUN(test_put_builds_payload);
    RUN(test_post_builds_payload);
    RUN(test_del_builds_delete);
    RUN(test_send_with_content_format);
    RUN(test_send_with_token);
    RUN(test_query_parsing);
    RUN(test_multi_segment_path);

    // Empty messages
    RUN(test_ping_is_empty_con);
    RUN(test_sendReset_is_empty_rst);

    // Responses
    RUN(test_sendResponse_basic);
    RUN(test_sendObserveResponse_includes_observe);
    RUN(test_observe_uint_encoding_lengths);

    // loop()
    RUN(test_loop_dispatches_to_server);
    RUN(test_loop_multi_segment_url);
    RUN(test_loop_unknown_url_sends_not_found);
    RUN(test_loop_ack_calls_response);
    RUN(test_loop_observe_request_flags);
    RUN(test_loop_ignores_malformed);
    RUN(test_loop_responds_to_ping_with_reset);
    RUN(test_loop_ignores_empty_reset);

    // Observers + notify
    RUN(test_addObserver_and_notify);
    RUN(test_notify_increments_sequence);
    RUN(test_addObserver_dedup);
    RUN(test_addObserver_capacity);
    RUN(test_removeObserver);
    RUN(test_addObserver_rejects_invalid);
    RUN(test_notify_lease_expiry);
    RUN(test_notify_observer_object);
    RUN(test_observer_token_clamped);

    // Integration tests that mirror how the examples/ sketches use the library.
    printf("\n== Example usage integration tests ==\n");
    runExampleTests();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
