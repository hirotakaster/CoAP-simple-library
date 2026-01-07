/*
 * A CoAP server with Observe functionality.
 *
 * Author: Pasquale Lafiosca (2025)
 */
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <coap-simple.h>

// --- DEBUG MACROS ---
// Set to 1 to enable Serial output for debugging. Set to 0 to disable all Serial calls (no-ops).
#define ENABLE_SERIAL_DEBUG 1

#if ENABLE_SERIAL_DEBUG
#define SERIAL_BEGIN(baud) Serial.begin(baud)
#define SERIAL_WHILE_WAIT \
    while (!Serial)       \
        ;
#define SERIAL_PRINT(x) Serial.print(x)
#define SERIAL_PRINT_HEX(x) Serial.print(x, HEX)
#define SERIAL_PRINTLN(x) Serial.println(x)
#define SERIAL_WRITE(x) Serial.write(x)
#else
// Define the macros as empty (no-op) when debugging is disabled
#define SERIAL_BEGIN(baud)
#define SERIAL_WHILE_WAIT
#define SERIAL_PRINT(x)
#define SERIAL_PRINT_HEX(x)
#define SERIAL_PRINTLN(x)
#define SERIAL_WRITE(x)
#endif

// UDP and CoAP class
EthernetUDP Udp;
Coap coap(Udp);

// Using a sequential identifier. IP and MAC will be based on this.
#define DEVICE_ID 1

byte mac[] = {0xBE, 0xEF, 0xBE, 0xEF, 0x00, DEVICE_ID}; // Define the MAC address, this must be unique.
IPAddress ip(192, 168, 0, DEVICE_ID);                   // This device IP.

// Declarations.
void endpoint_subscribe(CoapPacket &packet, IPAddress ip, int port);
void response_callback(CoapPacket &packet, IPAddress ip, int port);

void setup()
{

    // Initialize serial and wait for port to open.
    SERIAL_BEGIN(115200);
    SERIAL_WHILE_WAIT;
    SERIAL_PRINTLN("Booting...");
    SERIAL_PRINT("Configuring Ethernet...");
    Ethernet.begin(mac, ip);
    // Check for hardware issues
    if (Ethernet.hardwareStatus() == EthernetNoHardware)
    {
        SERIAL_PRINTLN("Ethernet shield/hardware not found. Check connections!");
        return;
    }

    SERIAL_PRINTLN("Setup echo endpoint");
    coap.server(endpoint_subscribe, "subscribe");

    // start coap server/client
    coap.start();

    SERIAL_PRINTLN("Server OK");
    SERIAL_PRINT("Server listening on ");
    SERIAL_PRINTLN(Ethernet.localIP());

    SERIAL_PRINTLN("Initialisation completed");
}

void endpoint_subscribe(CoapPacket &packet, IPAddress ip, int port)
{
    uint32_t observeValue = 0;
    if (!packet.getObserveValue(observeValue))
    {
        // No (or invalid) Observe option: treat as a normal GET on an observable resource.
        char payload[6]; // Max 5 digits for uint16_t + 1 for null terminator '\0'
        sprintf(payload, "%u", 42);
        int payload_len = strlen(payload);

        coap.sendResponse(ip, port, packet.messageid, payload, payload_len, COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
        SERIAL_PRINTLN("Handled non-observe GET on /subscribe");
        return;
    }

    // Observe=0 register, Observe=1 deregister.
    if (observeValue == 0)
    {
        if (!coap.addObserver("subscribe", ip, port, packet.token, packet.tokenlen))
        {
            coap.sendResponse(ip, port, packet.messageid, "busy", strlen("busy"), COAP_SERVICE_UNAVAILABLE, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
            SERIAL_PRINTLN("Observer table full; refused");
            return;
        }

        // Initial observe response includes Observe option and echoes token.
        coap.sendObserveResponse(ip, port, packet.messageid, "subscribed", strlen("subscribed"), COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen, 0);
        SERIAL_PRINTLN("Subscribed!");
    }
    else if (observeValue == 1)
    {
        coap.removeObserver("subscribe", ip, port, packet.token, packet.tokenlen);
        coap.sendResponse(ip, port, packet.messageid, "unsubscribed", strlen("unsubscribed"), COAP_CONTENT, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
        SERIAL_PRINTLN("Unsubscribed!");
    }
    else
    {
        coap.sendResponse(ip, port, packet.messageid, "invalid", strlen("invalid"), COAP_BAD_OPTION, COAP_TEXT_PLAIN, packet.token, packet.tokenlen);
        SERIAL_PRINTLN("Invalid Observe value");
    }
}

void loop()
{
    coap.loop(); // Keeps connection alive.

    send_notification();
}

// Demo notification with gibberish data.
void send_notification()
{
    static unsigned long lastNotifyMs = 0;
    const unsigned long notifyIntervalMs = 1000;
    unsigned long now = millis();
    if ((unsigned long)(now - lastNotifyMs) < notifyIntervalMs)
    {
        return;
    }
    lastNotifyMs = now;

    char payload[6]; // Max 5 digits for uint16_t + 1 for null terminator '\0'
    sprintf(payload, "%u", 42);
    int payload_len = strlen(payload);

    if (coap.notify("subscribe", payload, payload_len, COAP_TEXT_PLAIN) > 0)
    {
        SERIAL_PRINTLN("Notified!");
    }
}
