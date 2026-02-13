/**
 * @file http_client_test.c
 * @brief HTTP client shell test: http get/post, https get/post (with generic CA)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "Log/debug.h"
#include "mem.h"
#include "http_client.h"
#include "http_client_test.h"
#include "ms_network.h"

#define HTTP_TEST_READ_BUF_SIZE  (1024)
#define HTTP_TEST_MAX_BODY_PRINT (204800)

/* ISRG Root X1 (Let's Encrypt) - https://letsencrypt.org/certs/isrgrootx1.pem */
#define HTTPS_TEST_CA_ISRG_X1_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"

/* GlobalSign Root CA - R3 (Baidu, GlobalSign-issued sites) */
#define HTTPS_TEST_CA_GLOBALSIGN_R3_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n" \
"A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n" \
"Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n" \
"MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n" \
"A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n" \
"hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n" \
"RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n" \
"gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n" \
"KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n" \
"QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n" \
"XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n" \
"DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n" \
"LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n" \
"RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n" \
"jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n" \
"6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n" \
"mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n" \
"Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n" \
"WD9f\n" \
"-----END CERTIFICATE-----\n"

/* DigiCert Global Root G2 (DigiCert/GeoTrust, many CDNs and enterprises) */
#define HTTPS_TEST_CA_DIGICERT_G2_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1N\n" \
"GFdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n"

/* Common CA bundle: Let's Encrypt + GlobalSign R3 + DigiCert G2 (mbedtls parses multiple PEMs) */
#define HTTPS_TEST_CA_PEM_BUNDLE \
    HTTPS_TEST_CA_ISRG_X1_PEM \
    HTTPS_TEST_CA_GLOBALSIGN_R3_PEM \
    HTTPS_TEST_CA_DIGICERT_G2_PEM

/* Global CA bundle for HTTPS verification */
static network_tls_config_t https_test_tls_config = {
    .is_verify_hostname = 0,
    .ca_data = HTTPS_TEST_CA_PEM_BUNDLE,
    .ca_len = 0,
    .client_cert_data = NULL,
    .client_cert_len = 0,
    .client_key_data = NULL,
    .client_key_len = 0,
};

static void http_test_help(void)
{
    LOG_SIMPLE("Usage: http get <url>");
    LOG_SIMPLE("       http post <url> [body]");
    LOG_SIMPLE("       http put <url> [content]  (upload content as text/plain .txt)");
    LOG_SIMPLE("       https get <url> [verify|noverify]");
    LOG_SIMPLE("       https post <url> [body] [verify|noverify]");
    LOG_SIMPLE("       https put <url> [content] [verify|noverify]  (upload content as text/plain .txt)");
    LOG_SIMPLE("  verify=verify cert+SNI (default, e.g. Baidu); noverify=skip verify (test only, e.g. httpbin)");
    LOG_SIMPLE("Example: https get https://www.baidu.com");
    LOG_SIMPLE("         https get https://httpbin.org/get noverify");
    LOG_SIMPLE("         http put http://httpbin.org/put \"hello world\"  (upload txt content)");
}

static int http_client_test_cmd_deal(int argc, char *argv[])
{
    http_client_handle_t client = NULL;
    http_client_config_t config = { 0 };
    char *url = NULL;
    char *read_buf = NULL;
    int ret, r, total_print;
    int64_t content_len;
    int is_https_cmd;  /* 1 = user typed "https get/post ...", 0 = "http get/post ..." */

    if (argc < 2) {
        http_test_help();
        return -1;
    }

    is_https_cmd = (strcmp(argv[0], "https") == 0);

    if (is_https_cmd) {
        /* https get/post/put <url> ... */
        int use_noverify = 0;
        if (argc < 3) {
            LOG_SIMPLE("Usage: https get <url> [verify|noverify] | https post <url> [body] [verify|noverify] | https put <url> [content] [verify|noverify]");
            return -1;
        }
        url = argv[2];
        if (strncmp(url, "https://", 8) != 0) {
            LOG_SIMPLE("HTTPS command requires https:// URL.");
            return -1;
        }
        config.url = url;
        if (strcmp(argv[1], "get") == 0) {
            config.method = HTTP_METHOD_GET;
            if (argc >= 4 && (strcmp(argv[3], "noverify") == 0 || strcmp(argv[3], "verify") == 0))
                use_noverify = (strcmp(argv[3], "noverify") == 0);
        } else if (strcmp(argv[1], "post") == 0) {
            config.method = HTTP_METHOD_POST;
            if (argc >= 4 && argv[3][0] != '\0' && strcmp(argv[3], "noverify") != 0 && strcmp(argv[3], "verify") != 0) {
                config.post_data = argv[3];
                config.post_len = (int)strlen(argv[3]);
                config.content_type = "application/json";
                if (argc >= 5 && (strcmp(argv[4], "noverify") == 0 || strcmp(argv[4], "verify") == 0))
                    use_noverify = (strcmp(argv[4], "noverify") == 0);
            } else if (argc >= 4 && (strcmp(argv[3], "noverify") == 0 || strcmp(argv[3], "verify") == 0))
                use_noverify = (strcmp(argv[3], "noverify") == 0);
        } else if (strcmp(argv[1], "put") == 0) {
            config.method = HTTP_METHOD_PUT;
            /* PUT: upload [content] as text/plain (txt file) */
            if (argc >= 4 && argv[3][0] != '\0' && strcmp(argv[3], "noverify") != 0 && strcmp(argv[3], "verify") != 0) {
                config.post_data = argv[3];
                config.post_len = (int)strlen(argv[3]);
                config.content_type = "text/plain";
                if (argc >= 5 && (strcmp(argv[4], "noverify") == 0 || strcmp(argv[4], "verify") == 0))
                    use_noverify = (strcmp(argv[4], "noverify") == 0);
            } else if (argc >= 4 && (strcmp(argv[3], "noverify") == 0 || strcmp(argv[3], "verify") == 0))
                use_noverify = (strcmp(argv[3], "noverify") == 0);
        } else {
            LOG_SIMPLE("Usage: https get/post/put <url> ... [verify|noverify]");
            return -1;
        }
        https_test_tls_config.is_verify_hostname = use_noverify ? 0 : 1;
        config.tls_config = (const http_client_tls_config_t *)&https_test_tls_config;
    } else {
        /* http get <url> | http post <url> [body] | http put <url> [content] */
        if (strcmp(argv[1], "get") == 0) {
            if (argc < 3) {
                LOG_SIMPLE("Usage: http get <url>");
                return -1;
            }
            url = argv[2];
            config.url = url;
            config.method = HTTP_METHOD_GET;
        } else if (strcmp(argv[1], "post") == 0) {
            if (argc < 3) {
                LOG_SIMPLE("Usage: http post <url> [body]");
                return -1;
            }
            url = argv[2];
            config.url = url;
            config.method = HTTP_METHOD_POST;
            if (argc >= 4 && argv[3][0] != '\0') {
                config.post_data = argv[3];
                config.post_len = (int)strlen(argv[3]);
                config.content_type = "application/json";
            }
        } else if (strcmp(argv[1], "put") == 0) {
            if (argc < 3) {
                LOG_SIMPLE("Usage: http put <url> [content]  (upload content as text/plain .txt)");
                return -1;
            }
            url = argv[2];
            config.url = url;
            config.method = HTTP_METHOD_PUT;
            if (argc >= 4 && argv[3][0] != '\0') {
                config.post_data = argv[3];
                config.post_len = (int)strlen(argv[3]);
                config.content_type = "text/plain";
            }
        } else {
            http_test_help();
            return -1;
        }
    }

    client = http_client_init(&config);
    if (client == NULL) {
        LOG_SIMPLE("HTTP client init failed.");
        return -1;
    }

    ret = http_client_perform(client);
    if (ret != 0) {
        LOG_SIMPLE("HTTP perform failed (%d).", ret);
        http_client_cleanup(client);
        return -1;
    }

    ret = http_client_get_status_code(client);
    content_len = http_client_get_content_length(client);
    /* avoid int64_t printf (system may not support %lld/PRId64): print as int or two parts */
    if (content_len < 0) {
        LOG_SIMPLE("HTTP status: %d, content-length: -", ret);
    } else if (content_len <= (int64_t)INT_MAX) {
        LOG_SIMPLE("HTTP status: %d, content-length: %d", ret, (int)content_len);
    } else {
        LOG_SIMPLE("HTTP status: %d, content-length: %u%09u", ret,
                  (unsigned)((content_len / 1000000000) & 0xFFFFFFFFU),
                  (unsigned)(content_len % 1000000000));
    }

    read_buf = (char *)hal_mem_alloc(HTTP_TEST_READ_BUF_SIZE, MEM_LARGE);
    if (read_buf == NULL) {
        LOG_SIMPLE("Alloc read buffer failed.");
        http_client_cleanup(client);
        return -1;
    }
    total_print = 0;
    while ((r = http_client_read(client, read_buf, HTTP_TEST_READ_BUF_SIZE - 1)) > 0) {
        read_buf[r] = '\0';
        if (total_print + r <= HTTP_TEST_MAX_BODY_PRINT) {
            printf("%s", read_buf);
            total_print += r;
        } else if (total_print < HTTP_TEST_MAX_BODY_PRINT) {
            int remain = HTTP_TEST_MAX_BODY_PRINT - total_print;
            if (remain > 0) {
                int i = 0;
                while (i < r && i < remain) {
                    putchar(read_buf[i]);
                    i++;
                }
                total_print += i;
            }
            LOG_SIMPLE("");
            LOG_SIMPLE("... (truncated, total received %d bytes)", r);
        }
    }
    if (total_print > 0 && total_print <= HTTP_TEST_MAX_BODY_PRINT)
        printf("\r\n");
    if (r < 0)
        LOG_SIMPLE("HTTP read error (%d).", r);
    else
        ; /* r==0: normal EOF, body read complete */

    hal_mem_free(read_buf);
    http_client_cleanup(client);
    return 0;
}

static debug_cmd_reg_t http_client_test_cmd_table[] = {
    { "http",  "http get/post/put (get <url> | post <url> [body] | put <url> [content] as txt).", http_client_test_cmd_deal },
    { "https", "https get/post/put (get/post/put <url> ... [verify|noverify]).", http_client_test_cmd_deal },
};

static void http_client_test_cmd_register(void)
{
    debug_cmdline_register(http_client_test_cmd_table,
                           sizeof(http_client_test_cmd_table) / sizeof(http_client_test_cmd_table[0]));
}

void http_client_test_register(void)
{
    /* one callback registers both "http" and "https" from cmd_table */
    driver_cmd_register_callback("http", http_client_test_cmd_register);
}
