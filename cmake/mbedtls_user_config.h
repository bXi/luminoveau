// Appended to Mbed TLS's stock configuration via MBEDTLS_USER_CONFIG_FILE.
//
// libdatachannel's Mbed TLS backend names the DTLS-SRTP profile symbols unconditionally —
// srtpSupportedProtectionProfiles in src/impl/dtlstransport.cpp sits outside the
// RTC_ENABLE_MEDIA guard — so they are needed even with NO_MEDIA. Mbed TLS ships the feature
// off. Turning it on here keeps both libraries unpatched.

#define MBEDTLS_SSL_DTLS_SRTP
