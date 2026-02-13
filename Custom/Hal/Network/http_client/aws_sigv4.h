/**
 * @file aws_sigv4.h
 * @brief AWS SigV4 signing helper (pre-signed URL, query auth)
 *
 * This module is independent of http_client; it uses mbedTLS for crypto and drtc for UTC time.
 */

#ifndef __AWS_SIGV4_H__
#define __AWS_SIGV4_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build AWS SigV4 pre-signed URL (query authentication).
 *
 * Only builds the URL, no network I/O is performed.
 *
 * All strings must be ASCII and zero-terminated.
 *
 * @param method     HTTP method string, e.g. "GET" / "PUT" / "POST"
 * @param host       Host name, e.g. "s3.amazonaws.com"
 * @param region     AWS region, e.g. "us-east-1"
 * @param service    AWS service name, e.g. "s3" / "ec2"
 * @param path       Absolute URI path starting with '/', e.g. "/bucket/key"
 * @param query      Original query string WITHOUT leading '?', may be NULL or "".
 *                   Example: "Action=DescribeInstances&Version=2016-11-15".
 *                   For strict SigV4 compliance, parameters must be URL-encoded
 *                   and in canonical (alphabetical) order.
 * @param access_key AWS access key ID
 * @param secret_key AWS secret key
 * @param expires    Expiration seconds for X-Amz-Expires (1-604800)
 *                   Date/time for signing is taken from RTC (drtc) in UTC internally.
 * @param unsigned_payload 0 = use SHA256("") for GET/no body; 1 = use "UNSIGNED-PAYLOAD"
 *                         for S3 PUT (client must send header x-amz-content-sha256: UNSIGNED-PAYLOAD).
 * @param out_url    Output buffer to store final URL (zero-terminated)
 * @param out_len    Size of output buffer in bytes
 *
 * @return 0 on success, negative value on error.
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
                          int out_len);

#ifdef __cplusplus
}
#endif

#endif /* __AWS_SIGV4_H__ */

