/**
 * @file rtsp_digest_auth.h
 * @brief RTSP Digest authentication (RFC 2069 / RFC 2617)
 */

#ifndef RTSP_DIGEST_AUTH_H
#define RTSP_DIGEST_AUTH_H

#include "aicam_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate a random nonce for Digest auth challenge
 * @param nonce Output buffer
 * @param size Buffer size (should be >= 65 for 32-byte hex + null)
 */
void rtsp_digest_generate_nonce(char *nonce, size_t size);

/**
 * @brief Build WWW-Authenticate header value
 * @param nonce Nonce to include
 * @param realm Realm string
 * @param header Output buffer
 * @param size Buffer size
 */
void rtsp_digest_build_challenge(const char *nonce, const char *realm,
                                  char *header, size_t size);

/**
 * @brief Verify Digest Authorization header
 * @param auth_header Full Authorization header value
 * @param method RTSP method name (e.g. "DESCRIBE")
 * @param username Expected username
 * @param password Expected password
 * @param nonce Nonce that was sent in 401 challenge
 * @param realm Realm string
 * @return AICAM_TRUE if auth succeeds, AICAM_FALSE otherwise
 */
aicam_bool_t rtsp_digest_verify(const char *auth_header, const char *method,
                                 const char *username, const char *password,
                                 const char *nonce, const char *realm);

#ifdef __cplusplus
}
#endif

#endif /* RTSP_DIGEST_AUTH_H */
