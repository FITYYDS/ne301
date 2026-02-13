/**
 * @file aws_capture.c
 * @brief AWS S3 capture upload test: init (region, bucket, AK, SK) and upload via PUT presigned URL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Log/debug.h"
#include "mem.h"
#include "ms_network.h"
#include "drtc.h"
#include "device_service.h"
#include "http_client.h"
#include "aws_sigv4.h"
#include "aws_capture.h"

/* CA bundle for HTTPS (same as http_client_test.c); SNI not verified (is_verify_hostname = 0). */
#define AWS_CA_ISRG_X1_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"

#define AWS_CA_GLOBALSIGN_R3_PEM \
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

#define AWS_CA_DIGICERT_G2_PEM \
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

#define AWS_CA_PEM_BUNDLE \
    AWS_CA_ISRG_X1_PEM \
    AWS_CA_GLOBALSIGN_R3_PEM \
    AWS_CA_DIGICERT_G2_PEM

static network_tls_config_t aws_s3_tls_config = {
    .is_verify_hostname = 0,   /* do not verify SNI */
    .ca_data = AWS_CA_PEM_BUNDLE,
    .ca_len = 0,
    .client_cert_data = NULL,
    .client_cert_len = 0,
    .client_key_data = NULL,
    .client_key_len = 0,
};

#define AWS_CAPTURE_MAX_REGION   32
#define AWS_CAPTURE_MAX_BUCKET   64
#define AWS_CAPTURE_MAX_AK       64
#define AWS_CAPTURE_MAX_SK       128
#define AWS_CAPTURE_MAX_KEY      80
#define AWS_CAPTURE_MAX_HOST     160
#define AWS_CAPTURE_MAX_URL      2048
#define AWS_CAPTURE_PRESIGN_EXPIRES  3600

static struct {
    char region[AWS_CAPTURE_MAX_REGION];
    char bucket[AWS_CAPTURE_MAX_BUCKET];
    char access_key[AWS_CAPTURE_MAX_AK];
    char secret_key[AWS_CAPTURE_MAX_SK];
    int inited;
} g_aws_capture;

static void aws_capture_help(void)
{
    LOG_SIMPLE("aws_capture init <region> <bucket> <AK> <SK>  - init AWS S3 (e.g. cn-north-1 milesight-msc-cn-dev-temp AKIA... sk...)");
    LOG_SIMPLE("aws_capture upload                              - capture image and PUT to S3");
}

/** Build S3 host from bucket and region (China: .amazonaws.com.cn, else .amazonaws.com). */
static void aws_capture_build_host(const char *bucket, const char *region, char *host, size_t host_len)
{
    if (strncmp(region, "cn-", 3) == 0)
        snprintf(host, host_len, "%s.s3.%s.amazonaws.com.cn", bucket, region);
    else
        snprintf(host, host_len, "%s.s3.%s.amazonaws.com", bucket, region);
}

/** Build object key from RTC local time: capture_YYYYMMDD_HHMMSS.jpg */
static void aws_capture_build_key(char *key, size_t key_len)
{
    RTC_TIME_S t;
    uint64_t ts = rtc_get_timeStamp();
    if (ts == 0) {
        snprintf(key, key_len, "capture_unknown.jpg");
        return;
    }
    timeStamp_to_time(ts, &t);
    snprintf(key, key_len, "capture_%04u%02u%02u_%02u%02u%02u.jpg",
             (unsigned)(1970U + t.year), (unsigned)t.month, (unsigned)t.date,
             (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
}

static int aws_capture_cmd_deal(int argc, char *argv[])
{
    if (argc < 2) {
        aws_capture_help();
        return -1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc < 6) {
            LOG_SIMPLE("Usage: aws_capture init <region> <bucket> <AK> <SK>");
            return -1;
        }
        if (strlen(argv[2]) >= AWS_CAPTURE_MAX_REGION ||
            strlen(argv[3]) >= AWS_CAPTURE_MAX_BUCKET ||
            strlen(argv[4]) >= AWS_CAPTURE_MAX_AK ||
            strlen(argv[5]) >= AWS_CAPTURE_MAX_SK) {
            LOG_SIMPLE("aws_capture init: argument too long.");
            return -1;
        }
        strncpy(g_aws_capture.region, argv[2], AWS_CAPTURE_MAX_REGION - 1);
        g_aws_capture.region[AWS_CAPTURE_MAX_REGION - 1] = '\0';
        strncpy(g_aws_capture.bucket, argv[3], AWS_CAPTURE_MAX_BUCKET - 1);
        g_aws_capture.bucket[AWS_CAPTURE_MAX_BUCKET - 1] = '\0';
        strncpy(g_aws_capture.access_key, argv[4], AWS_CAPTURE_MAX_AK - 1);
        g_aws_capture.access_key[AWS_CAPTURE_MAX_AK - 1] = '\0';
        strncpy(g_aws_capture.secret_key, argv[5], AWS_CAPTURE_MAX_SK - 1);
        g_aws_capture.secret_key[AWS_CAPTURE_MAX_SK - 1] = '\0';
        g_aws_capture.inited = 1;
        LOG_SIMPLE("aws_capture init ok: region=%s bucket=%s", g_aws_capture.region, g_aws_capture.bucket);
        return 0;
    }

    if (strcmp(argv[1], "upload") == 0) {
        if (!g_aws_capture.inited) {
            LOG_SIMPLE("aws_capture: run 'aws_capture init <region> <bucket> <AK> <SK>' first.");
            return -1;
        }

        uint8_t *jpeg_buf = NULL;
        int jpeg_len = 0;
        aicam_result_t cap_ret = device_service_camera_capture(&jpeg_buf, &jpeg_len,
                                                               AICAM_FALSE, NULL, NULL);
        if (cap_ret != AICAM_OK || jpeg_buf == NULL || jpeg_len <= 0) {
            LOG_SIMPLE("aws_capture upload: capture failed (ret=%d, len=%d).", (int)cap_ret, jpeg_len);
            return -1;
        }
        LOG_SIMPLE("aws_capture upload: capture ok, len=%d", jpeg_len);

        char host[AWS_CAPTURE_MAX_HOST];
        char key[AWS_CAPTURE_MAX_KEY];
        char path[AWS_CAPTURE_MAX_KEY + 2];
        char presigned_url[AWS_CAPTURE_MAX_URL];

        aws_capture_build_host(g_aws_capture.bucket, g_aws_capture.region, host, sizeof(host));
        aws_capture_build_key(key, sizeof(key));
        snprintf(path, sizeof(path), "/%s", key);
        LOG_SIMPLE("aws_capture upload: host=%s key=%s", host, key);

        int sign_ret = aws_sigv4_presign_url("PUT", host, g_aws_capture.region, "s3",
                                            path, NULL,
                                            g_aws_capture.access_key, g_aws_capture.secret_key,
                                            AWS_CAPTURE_PRESIGN_EXPIRES, 1 /* unsigned_payload */,
                                            presigned_url, sizeof(presigned_url));
        if (sign_ret != 0) {
            LOG_SIMPLE("aws_capture upload: presign failed (%d).", sign_ret);
            device_service_camera_free_jpeg_buffer(jpeg_buf);
            return -1;
        }
        LOG_SIMPLE("aws_capture upload: presign ok, PUT %d bytes", jpeg_len);
        printf("aws_capture upload: url=%s\r\n", presigned_url);

        http_client_config_t config = { 0 };
        config.url = presigned_url;
        config.method = HTTP_METHOD_PUT;
        config.post_data = (const char *)jpeg_buf;
        config.post_len = jpeg_len;
        config.content_type = "image/jpeg";
        config.timeout_ms = 60000;   /* upload + S3 response */
        config.buffer_size = 8192;
        config.tls_config = (const http_client_tls_config_t *)&aws_s3_tls_config;  /* HTTPS with CA, no SNI verify */

        http_client_handle_t client = http_client_init(&config);
        if (client == NULL) {
            LOG_SIMPLE("aws_capture upload: http_client_init failed.");
            device_service_camera_free_jpeg_buffer(jpeg_buf);
            return -1;
        }
        if (http_client_set_header(client, "x-amz-content-sha256", "UNSIGNED-PAYLOAD") != 0) {
            LOG_SIMPLE("aws_capture upload: set header failed.");
            http_client_cleanup(client);
            device_service_camera_free_jpeg_buffer(jpeg_buf);
            return -1;
        }

        LOG_SIMPLE("aws_capture upload: sending request (headers + body %d bytes)...", jpeg_len);
        int perf_ret = http_client_perform(client);
        int status = http_client_get_status_code(client);
        /* Drain response body so connection is fully read before close (avoids recv error on half-closed) */
        if (perf_ret == 0) {
            char drain_buf[256];
            while (http_client_read(client, drain_buf, sizeof(drain_buf)) > 0)
                ;
        }
        http_client_cleanup(client);
        device_service_camera_free_jpeg_buffer(jpeg_buf);

        if (perf_ret != 0) {
            const char *err_str = "unknown";
            if (perf_ret == -0x1000 - 3) err_str = "parse_url";
            else if (perf_ret == -0x1000 - 4) err_str = "connect";
            else if (perf_ret == -0x1000 - 5) err_str = "send";
            else if (perf_ret == -0x1000 - 6) err_str = "recv";
            else if (perf_ret == -0x1000 - 7) err_str = "parse";
            LOG_SIMPLE("aws_capture upload: HTTP perform failed (ret=%d, %s).", perf_ret, err_str);
            if (status > 0)
                LOG_SIMPLE("aws_capture upload: response status=%d (before recv complete).", status);
            return -1;
        }
        if (status != 200) {
            LOG_SIMPLE("aws_capture upload: HTTP status %d (expected 200).", status);
            return -1;
        }
        LOG_SIMPLE("aws_capture upload ok: key=%s len=%d", key, jpeg_len);
        return 0;
    }

    aws_capture_help();
    return -1;
}

static debug_cmd_reg_t aws_capture_cmd_table[] = {
    { "aws_capture", "AWS S3 capture upload: init <region> <bucket> <AK> <SK>, upload.", aws_capture_cmd_deal },
};

static void aws_capture_cmd_register(void)
{
    debug_cmdline_register(aws_capture_cmd_table,
                           sizeof(aws_capture_cmd_table) / sizeof(aws_capture_cmd_table[0]));
}

void aws_capture_register(void)
{
    driver_cmd_register_callback("aws_capture", aws_capture_cmd_register);
}
