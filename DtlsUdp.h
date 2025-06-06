#if defined(ESP32)
// DtlsUdp.h
// mbedTLS DTLS wrapper class skeleton for Arduino
// Example implementation: UDP-compatible API, DTLS communication using mbedTLS

#ifndef __DTLS_UDP_H__
#define __DTLS_UDP_H__

#include <Arduino.h>
#include <IPAddress.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include "Udp.h"

// NOTE: This class is only available for ESP32 because it depends on mbedtls, which is provided by the ESP32 Arduino core.

class DtlsUdp : public UDP {
public:
    DtlsUdp();
    ~DtlsUdp();
    // UDPインターフェースの実装
    uint8_t begin(uint16_t port) override;
    void stop() override;
    int beginPacket(IPAddress ip, uint16_t port) override;
    int beginPacket(const char *host, uint16_t port) override;
    int endPacket() override;
    size_t write(const uint8_t *buf, size_t size) override;
    size_t write(uint8_t data) override;
    int parsePacket() override;
    int available() override;
    int read(unsigned char* buffer, size_t len) override;
    int read(char* buffer, size_t len) override;
    int read() override;
    int peek() override;
    void flush() override;
    IPAddress remoteIP() override;
    uint16_t remotePort() override;
    // DTLS独自
    bool connect(IPAddress ip, int port);
    void end();
    // --- 証明書/鍵の設定用API ---
    bool setRootCA(const char* ca_pem);
    bool setClientCert(const char* cert_pem, const char* key_pem);
private:
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_net_context net_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca_cert;
    mbedtls_x509_crt client_cert;
    mbedtls_pk_context client_key;
    bool connected;
    IPAddress _remoteIP;
    uint16_t _remotePort;
};

#endif // __DTLS_UDP_H__

#endif // ESP32
