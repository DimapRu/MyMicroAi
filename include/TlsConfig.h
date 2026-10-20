#pragma once

#include <WiFiClientSecure.h>

// Mozilla root CA bundle embedded by platformio.ini (board_build.embed_files).
// Regenerate with ESP-IDF v4.4 gen_crt_bundle.py; see README "TLS".
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

inline void configureTls(WiFiClientSecure& client) {
    client.setCACertBundle(rootca_crt_bundle_start);
}
