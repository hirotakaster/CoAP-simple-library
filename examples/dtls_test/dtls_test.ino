// NOTE: This sketch is only available for ESP32 because it depends on mbedtls, which is provided by the ESP32 Arduino core.
// dtls_test.ino
// DTLS CoAP client auto test sketch
// Checks GET response from libcoap server
#include <SPI.h>
#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet.h>
#include <coap-simple.h>
#include "DtlsUdp.h"

byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
IPAddress dev_ip(10,10,10,10); // Change as needed

const int LED_PIN = 13;
DtlsUdp dtlsUdp;
Coap coap(dtlsUdp);
bool testPassed = false;

// Let's Encrypt ISRG Root X1
const char lets_encrypt_root_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgISA7lE0QJzi10BGiGgF2pQb7MA0GCSqGSIb3DQEBCwUA\n"
"MEoxCzAJBgNVBAYTAlVTMRMwEQYDVQQKEwpJbnRlcm5ldCBTZWN1cml0eTEhMB8G\n"
"A1UEAxMYSVNSRyBSb290IFgxIE5ldHdvcmsgQ0EwHhcNMTQwNzE2MTIxMzQwWhcN\n"
"MzQwNzE2MTIxMzQwWjBKMQswCQYDVQQGEwJVUzETMBEGA1UEChMKSW50ZXJuZXQg\n"
"U2VjdXJpdHkxITAfBgNVBAMTGEhSRyBSb290IFgxIE5ldHdvcmsgQ0EwggIiMA0G\n"
"CSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQC7Ahh1gC1XkQwZ0pniS3pSkeCZMt2g\n"
"kFh2RFxYDs2fzGEGZm4G6dkprdFM5lTyBC0bYKT1eZq9VHtV6nRvWmvMRGsiE9z1\n"
"k5lFfW4rWuIwoMvVJE9idh+NangNh4tW7x1YgnSUZXoqBYwygJyI072QtdgQXl3k\n"
"n2Ue+a83H8XIv2qxGn8pY/+bexdFv+DE5jBqFaUG2yygxN6E466+vWXTjhBMWent\n"
"lMPmMXxMvmXrKE+g6u40qCMgfHdCqkfNNpJBbGAbIYW/W2PASi6DPd7OJbRRqtD9\n"
"pz50jdK5Zk90un0nLBKBPXn1HULICwhf66A1VpzwuNFuIBqmoeZaZX6mE6xPD58x\n"
"H5TADaBrZEcD3xKhsR4HIX66vepQP9enZ5bY3bT5iAG2wE8xmPKzW0fvk/sdzEYw\n"
"+nOiYzcuEGoVYLRNvKcJsteVEh9UpAJZciV06P88GJEqn3Ejj6inUeJ8V+RaHcRU\n"
"W2KIiMzFxljI0X58F3RrgPf63HgFUsVTNff7kwh28ykVfoCkb0z7dxyzKDn5Xxhx\n"
"nQIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB/zAdBgNV\n"
"HQ4EFgQUXxhxL7sRKqzZo4PMBVXgS5aXoaIwDQYJKoZIhvcNAQELBQADggEBAJk1\n"
"gQXl3k2I5OGucs5cjox96D65gis6pZeRAEIJ5zsxFrqOeE0zY4xXHzkwmo7aX6ix\n"
"bAGP82afgAvFp8ZUBKbjDg7sWXBJgp7wa9u0edPFsKnzGWx/ju0RduCMsZ/HXXIQ\n"
"t1vZ1tcHTul3e8DqRLVQjaxAg/P6nxsVXni4eWh05rq6ArlTc95xJMu38xpv8uK9\n"
"B6f+GO6zkRgZNpmjQe7YQDdyCjTiMQuuLHcEGoVYLRNvKcJsteVEh9UpAJZciV06\n"
"P88GJEqn3Ejj6inUeJ8V+RaHcRUW2KIiMzFxljI0X58F3RrgPf63HgFUsVTNff7k\n"
"wh28ykVfoCkb0z7dxyzKDn5XxhxnQ==\n"
"-----END CERTIFICATE-----\n";

void callback_response(CoapPacket &packet, IPAddress ip, int port) {
  Serial.println("[Coap DTLS Response got]");
  char p[packet.payloadlen + 1];
  memcpy(p, packet.payload, packet.payloadlen);
  p[packet.payloadlen] = NULL;
  Serial.print("Response: ");
  Serial.println(p);
  if (strstr(p, "Hello") != NULL) {
    testPassed = true;
    digitalWrite(LED_PIN, HIGH);
  } else {
    testPassed = false;
    digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Ethernet.begin(mac, dev_ip);
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());
  dtlsUdp.begin(0);
  dtlsUdp.setRootCA(lets_encrypt_root_pem); // Set Root CA
  dtlsUdp.connect(IPAddress(10,10,10,20), 5684);
  Serial.println("Setup Response Callback");
  coap.response(callback_response);
  coap.start();
}

void loop() {
  Serial.println("Send DTLS CoAP Test Request");
  int msgid = coap.get(IPAddress(10,10,10,20), 5684, "test");
  delay(2000);
  coap.loop();
  if (testPassed) {
    Serial.println("[TEST PASS] DTLS CoAP communication successful");
  } else {
    Serial.println("[TEST FAIL] Invalid response or communication failed");
  }
  delay(3000);
}
