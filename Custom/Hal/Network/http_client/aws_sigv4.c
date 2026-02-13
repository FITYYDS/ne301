/**
 * @file aws_sigv4.c
 * @brief AWS SigV4 signing helper (pre-signed URL, query authentication).
 *        Implements AWS Signature Version 4 for request signing via query parameters.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include "aws_sigv4.h"
#include "drtc.h"

/** Encode binary to lowercase hex string. */
static void aws_hex_encode(const unsigned char *in, size_t in_len, char *out_hex)
{
    static const char *hex = "0123456789abcdef";
    size_t i;
    for (i = 0; i < in_len; i++) {
        out_hex[i * 2]     = hex[(in[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = hex[in[i] & 0x0F];
    }
    out_hex[in_len * 2] = '\0';
}

/** Compute SHA256 of input and write result as 64-char lowercase hex string. */
static int aws_sha256_hex(const unsigned char *data, size_t len, char out_hex[65])
{
    unsigned char hash[32];
    if (mbedtls_sha256(data, len, hash, 0) != 0) {
        return -1;
    }
    aws_hex_encode(hash, sizeof(hash), out_hex);
    return 0;
}

/** HMAC-SHA256: output 32 bytes into out[]. */
static int aws_hmac_sha256(const unsigned char *key, size_t key_len,
                           const unsigned char *data, size_t data_len,
                           unsigned char out[32])
{
    int ret;
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (info == NULL) return -1;

    mbedtls_md_init(&ctx);
    ret = mbedtls_md_setup(&ctx, info, 1);
    if (ret == 0) ret = mbedtls_md_hmac_starts(&ctx, key, (unsigned int)key_len);
    if (ret == 0) ret = mbedtls_md_hmac_update(&ctx, data, data_len);
    if (ret == 0) ret = mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);

    return (ret == 0) ? 0 : -1;
}

/**
 * RFC3986-style URI encode. Unreserved chars (A-Z a-z 0-9 - . _ ~) are not encoded.
 * @param encode_slash 0 = leave '/' as-is (for path); 1 = encode '/' (for query).
 */
static int aws_uri_encode(const char *in, char *out, size_t out_size, int encode_slash)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    size_t i;

    if (out_size == 0) return -1;

    for (i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        int unreserved = (c >= 'A' && c <= 'Z') ||
                         (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') ||
                         c == '-' || c == '.' || c == '_' || c == '~';

        if (c == '/' && !encode_slash) {
            if (o + 1 >= out_size) return -1;
            out[o++] = c;
        } else if (unreserved) {
            if (o + 1 >= out_size) return -1;
            out[o++] = c;
        } else {
            if (o + 3 >= out_size) return -1;
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0x0F];
            out[o++] = hex[c & 0x0F];
        }
    }

    if (o >= out_size) return -1;
    out[o] = '\0';
    return 0;
}

/**
 * Convert UTC timestamp (seconds since 1970-01-01 00:00:00 UTC) to SigV4 date strings.
 * Fills amz_date as "YYYYMMDDTHHMMSSZ" and date as "YYYYMMDD".
 * Returns 0 on success, -1 on invalid timestamp.
 */
static int utc_timestamp_to_sigv4_date(uint64_t utc_ts, char amz_date[17], char date[9])
{
    static const uint16_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint64_t seconds = utc_ts;
    uint32_t days = (uint32_t)(seconds / 86400U);
    uint32_t rem  = (uint32_t)(seconds % 86400U);
    uint32_t hour = rem / 3600U;
    rem %= 3600U;
    uint32_t minute = rem / 60U;
    uint32_t second = rem % 60U;

    uint32_t year = 1970U;
    while (1) {
        uint32_t days_in_year = 365U;
        if ((year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U))
            days_in_year = 366U;
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }

    uint32_t month = 0U;
    while (month < 12U) {
        uint32_t dim = days_in_month[month];
        if (month == 1U && ((year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U)))
            dim = 29U;
        if (days >= dim) {
            days -= dim;
            month++;
        } else {
            break;
        }
    }
    uint32_t day = days + 1U;

    if (snprintf(date, 9, "%04lu%02lu%02lu", (unsigned long)year, (unsigned long)(month + 1U), (unsigned long)day) < 0)
        return -1;
    if (snprintf(amz_date, 17, "%04lu%02lu%02luT%02lu%02lu%02luZ",
                (unsigned long)year, (unsigned long)(month + 1U), (unsigned long)day,
                (unsigned long)hour, (unsigned long)minute, (unsigned long)second) < 0)
        return -1;
    return 0;
}

/** Derive SigV4 signing key: kSecret -> kDate -> kRegion -> kService -> kSigning. */
static int aws_sigv4_derive_signing_key(const char *secret_key,
                                        const char date[9],
                                        const char *region,
                                        const char *service,
                                        unsigned char out_key[32])
{
    unsigned char k_date[32];
    unsigned char k_region[32];
    unsigned char k_service[32];
    unsigned char k_signing[32];
    char k_secret[128];
    int n;

    if (secret_key == NULL || date == NULL || region == NULL || service == NULL || out_key == NULL)
        return -1;

    n = snprintf(k_secret, sizeof(k_secret), "AWS4%s", secret_key);
    if (n <= 0 || (size_t)n >= sizeof(k_secret)) return -1;

    if (aws_hmac_sha256((unsigned char *)k_secret, (size_t)n,
                        (const unsigned char *)date, strlen(date),
                        k_date) != 0)
        return -1;

    if (aws_hmac_sha256(k_date, sizeof(k_date),
                        (const unsigned char *)region, strlen(region),
                        k_region) != 0)
        return -1;

    if (aws_hmac_sha256(k_region, sizeof(k_region),
                        (const unsigned char *)service, strlen(service),
                        k_service) != 0)
        return -1;

    if (aws_hmac_sha256(k_service, sizeof(k_service),
                        (const unsigned char *)"aws4_request", strlen("aws4_request"),
                        k_signing) != 0)
        return -1;

    memcpy(out_key, k_signing, 32);
    return 0;
}

/**
 * Build AWS SigV4 pre-signed URL (query auth). Steps: canonical request ->
 * hash -> string-to-sign -> signing key -> HMAC -> hex signature -> final URL.
 */
int aws_sigv4_presign_url(const char *method,
                          const char *host,
                          const char *region,
                          const char *service,
                          const char *path,
                          const char *query,
                          const char *access_key,
                          const char *secret_key,
                          int expires,
                          int unsigned_payload,
                          char *out_url,
                          int out_len)
{
    int ret = -1;
    char amz_date[17];
    char date[9];
    char encoded_path[512];
    char canonical_query[2048];
    char canonical_request[4096];
    char canonical_request_hash[65];
    char credential_scope[256];
    char string_to_sign[4096];
    unsigned char signing_key[32];
    unsigned char signature_bin[32];
    char signature_hex[65];

    /* Payload hash: empty for GET; UNSIGNED-PAYLOAD for S3 PUT (client sends x-amz-content-sha256). */
    const char *payload_hash = (unsigned_payload != 0)
        ? "UNSIGNED-PAYLOAD"
        : "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char *signed_headers = (unsigned_payload != 0) ? "host;x-amz-content-sha256" : "host";
    char signed_headers_encoded[64];
    const char *signed_headers_for_query = signed_headers;
    if (aws_uri_encode(signed_headers, signed_headers_encoded, sizeof(signed_headers_encoded), 1) == 0)
        signed_headers_for_query = signed_headers_encoded;

    char access_and_scope[512];
    char credential_encoded[512];
    char query_prefix[1024];

    if (!method || !host || !region || !service ||
        !path || !access_key || !secret_key ||
        !out_url || out_len <= 0) {
        return -1;
    }

    /* Get UTC time from RTC and build amz_date / date for SigV4 */
    {
        uint64_t utc_ts = rtc_get_timeStamp();
        if (utc_ts == 0U) return -1;
        if (utc_timestamp_to_sigv4_date(utc_ts, amz_date, date) != 0) return -1;
    }

    if (!query) query = "";

    /* Step 1: Encode path for CanonicalURI */
    if (aws_uri_encode(path, encoded_path, sizeof(encoded_path), 0) != 0)
        return -1;

    /* Step 2: CredentialScope = date/region/service/aws4_request */
    if (snprintf(credential_scope, sizeof(credential_scope),
                 "%s/%s/%s/aws4_request", date, region, service) < 0)
        return -1;

    /* Step 3: X-Amz-Credential = URI-encode(access_key + "/" + credential_scope) */
    if (snprintf(access_and_scope, sizeof(access_and_scope),
                 "%s/%s", access_key, credential_scope) < 0)
        return -1;
    if (aws_uri_encode(access_and_scope, credential_encoded,
                       sizeof(credential_encoded), 1) != 0)
        return -1;

    /* Step 4: CanonicalQueryString: original query + X-Amz-* params.
     * Strict SigV4 requires all query params sorted by key; extend with
     * parse+sort if full compliance is needed.
     */
    if (query[0] != '\0') {
        if (snprintf(query_prefix, sizeof(query_prefix), "%s&", query) < 0)
            return -1;
    } else {
        query_prefix[0] = '\0';
    }

    if (snprintf(canonical_query, sizeof(canonical_query),
                 "%sX-Amz-Algorithm=AWS4-HMAC-SHA256"
                 "&X-Amz-Credential=%s"
                 "&X-Amz-Date=%s"
                 "&X-Amz-Expires=%d"
                 "&X-Amz-SignedHeaders=%s",
                 query_prefix,
                 credential_encoded,
                 amz_date,
                 expires,
                 signed_headers_for_query) < 0) {
        return -1;
    }

    /* Step 5: CanonicalHeaders (host; for S3 PUT unsigned add x-amz-content-sha256) */
    {
        char canonical_headers[384];

        if (unsigned_payload != 0) {
            if (snprintf(canonical_headers, sizeof(canonical_headers),
                         "host:%s\nx-amz-content-sha256:UNSIGNED-PAYLOAD\n", host) < 0)
                return -1;
        } else {
            if (snprintf(canonical_headers, sizeof(canonical_headers),
                         "host:%s\n", host) < 0)
                return -1;
        }

        /* Step 6: CanonicalRequest */
        if (snprintf(canonical_request, sizeof(canonical_request),
                     "%s\n"     /* HTTPMethod */
                     "%s\n"     /* CanonicalURI */
                     "%s\n"     /* CanonicalQueryString */
                     "%s\n"     /* CanonicalHeaders */
                     "%s\n"     /* SignedHeaders */
                     "%s",      /* HashedPayload */
                     method,
                     encoded_path,
                     canonical_query,
                     canonical_headers,
                     signed_headers,
                     payload_hash) < 0)
            return -1;
    }

    /* Step 7: SHA256(CanonicalRequest) */
    if (aws_sha256_hex((const unsigned char *)canonical_request,
                       strlen(canonical_request),
                       canonical_request_hash) != 0)
        return -1;

    /* Step 8: StringToSign */
    if (snprintf(string_to_sign, sizeof(string_to_sign),
                 "AWS4-HMAC-SHA256\n"
                 "%s\n"
                 "%s\n"
                 "%s",
                 amz_date,
                 credential_scope,
                 canonical_request_hash) < 0)
        return -1;

    /* Step 9: Derive signing key */
    if (aws_sigv4_derive_signing_key(secret_key, date, region, service,
                                     signing_key) != 0)
        return -1;

    /* Step 10: HMAC(signing_key, StringToSign) */
    if (aws_hmac_sha256(signing_key, sizeof(signing_key),
                        (const unsigned char *)string_to_sign,
                        strlen(string_to_sign),
                        signature_bin) != 0)
        return -1;

    aws_hex_encode(signature_bin, sizeof(signature_bin), signature_hex);

    /* Step 11: Final URL with X-Amz-Signature */
    if (snprintf(out_url, (size_t)out_len,
                 "https://%s%s?%s&X-Amz-Signature=%s",
                 host,
                 path,
                 canonical_query,
                 signature_hex) < 0)
        return -1;

    /* Ensure zero-terminated even if truncated */
    out_url[out_len - 1] = '\0';

    ret = 0;
    return ret;
}

