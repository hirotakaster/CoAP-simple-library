#if defined(ESP32)
// NOTE: This class is only available for ESP32 because it depends on mbedtls, which is provided by the ESP32 Arduino core.
// DtlsUdp.cpp
// mbedTLS DTLS wrapper class skeleton implementation for Arduino (WiFiUDP base)
#include "DtlsUdp.h"

DtlsUdp::DtlsUdp() : connected(false) {
    mbedtls_net_init(&net_ctx);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509_crt_init(&ca_cert);
    mbedtls_x509_crt_init(&client_cert);
    mbedtls_pk_init(&client_key);
}

DtlsUdp::~DtlsUdp() {
    end();
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_net_free(&net_ctx);
    mbedtls_x509_crt_free(&ca_cert);
    mbedtls_x509_crt_free(&client_cert);
    mbedtls_pk_free(&client_key);
}

uint8_t DtlsUdp::begin(uint16_t port) {
    // For DTLS: No need to initialize UDP socket (WiFiUDP base)
    return 1;
}

bool DtlsUdp::connect(IPAddress ip, int port) {
    char ipstr[16];
    sprintf(ipstr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (mbedtls_net_connect(&net_ctx, ipstr, portstr, MBEDTLS_NET_PROTO_UDP) != 0) return false;
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return false;
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (mbedtls_ssl_setup(&ssl, &conf) != 0) return false;
    mbedtls_ssl_set_bio(&ssl, &net_ctx, mbedtls_net_send, mbedtls_net_recv, NULL);
    // DTLS handshake
    int ret;
    do {
        ret = mbedtls_ssl_handshake(&ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    connected = (ret == 0);
    return connected;
}

bool DtlsUdp::connect(const char* host, int port) {
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (mbedtls_net_connect(&net_ctx, host, portstr, MBEDTLS_NET_PROTO_UDP) != 0) return false;
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return false;
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (mbedtls_ssl_setup(&ssl, &conf) != 0) return false;
    mbedtls_ssl_set_bio(&ssl, &net_ctx, mbedtls_net_send, mbedtls_net_recv, NULL);
    mbedtls_ssl_set_hostname(&ssl, host);
    // DTLS handshake
    int ret;
    do {
        ret = mbedtls_ssl_handshake(&ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    connected = (ret == 0);
    return connected;
}

int DtlsUdp::beginPacket(IPAddress ip, uint16_t port) {
    _remoteIP = ip;
    _remotePort = port;
    return 1;
}

int DtlsUdp::beginPacket(const char *host, uint16_t port) {
    // Hostname resolution not supported (implement if needed)
    return 0;
}

int DtlsUdp::endPacket() {
    // For DTLS: send is done directly in write
    return 1;
}

size_t DtlsUdp::write(const uint8_t *buf, size_t size) {
    if (!connected) return 0;
    return mbedtls_ssl_write(&ssl, buf, size);
}

size_t DtlsUdp::write(uint8_t data) {
    return write(&data, 1);
}

int DtlsUdp::parsePacket() {
    // DTLS is connection-oriented, always treat as one packet
    return 1;
}

int DtlsUdp::available() {
    // Check if receive buffer has data (simple implementation)
    return 1;
}

int DtlsUdp::read(unsigned char* buffer, size_t len) {
    if (!connected) return 0;
    return mbedtls_ssl_read(&ssl, buffer, len);
}

int DtlsUdp::read(char* buffer, size_t len) {
    if (!connected) return 0;
    return mbedtls_ssl_read(&ssl, (unsigned char*)buffer, len);
}

int DtlsUdp::read() {
    unsigned char b;
    return read(&b, 1) == 1 ? b : -1;
}

int DtlsUdp::peek() { return -1; }

void DtlsUdp::flush() {}

IPAddress DtlsUdp::remoteIP() { return _remoteIP; }

uint16_t DtlsUdp::remotePort() { return _remotePort; }

void DtlsUdp::end() {
    if (connected) {
        mbedtls_ssl_close_notify(&ssl);
        connected = false;
    }
}

void DtlsUdp::stop() {
    // For DTLS: nothing to do
}

bool DtlsUdp::setRootCA(const char* ca_pem) {
    mbedtls_x509_crt_free(&ca_cert);
    mbedtls_x509_crt_init(&ca_cert);
    int ret = mbedtls_x509_crt_parse(&ca_cert, (const unsigned char*)ca_pem, strlen(ca_pem)+1);
    if (ret != 0) return false;
    mbedtls_ssl_conf_ca_chain(&conf, &ca_cert, NULL);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    return true;
}

bool DtlsUdp::setClientCert(const char* cert_pem, const char* key_pem) {
    mbedtls_x509_crt_free(&client_cert);
    mbedtls_x509_crt_init(&client_cert);
    mbedtls_pk_free(&client_key);
    mbedtls_pk_init(&client_key);
    int ret1 = mbedtls_x509_crt_parse(&client_cert, (const unsigned char*)cert_pem, strlen(cert_pem)+1);
    int ret2 = mbedtls_pk_parse_key(&client_key, (const unsigned char*)key_pem, strlen(key_pem)+1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret1 != 0 || ret2 != 0) return false;
    mbedtls_ssl_conf_own_cert(&conf, &client_cert, &client_key);
    return true;
}
#endif // ESP32