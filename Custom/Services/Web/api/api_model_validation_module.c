/**
 * @file api_model_validation_module.c
 * @brief Model Validation API Module
 * @details API module for model inference validation and testing
 */

#include "web_api.h"
#include "ai_service.h"
#include "cJSON.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "api_model_validation_module.h"
#include "web_server.h"
#include "buffer_mgr.h"




/* ==================== Internal Functions ==================== */

/**
 * @brief Base64 encode data
 */
static char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    *output_length = 4 * ((input_length + 2) / 3);
    char* encoded_data = (char*)buffer_calloc(1, *output_length + 1);
    if (!encoded_data) return NULL;
    
    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t a = i < input_length ? data[i++] : 0;
        uint32_t b = i < input_length ? data[i++] : 0;
        uint32_t c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (a << 0x10) + (b << 0x08) + c;
        
        encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    for (i = 0; i < (3 - input_length % 3) % 3; i++) {
        encoded_data[*output_length - 1 - i] = '=';
    }
    
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

/**
 * @brief Find a substring in a string
 */
static const char *memfind(const char *haystack, size_t haystack_len,
                           const char *needle, size_t needle_len)
{
    if (!haystack || !needle || haystack_len < needle_len)
        return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; i++)
    {
        if (memcmp(haystack + i, needle, needle_len) == 0)
        {
            return haystack + i;
        }
    }
    return NULL;
}

/**
 * @brief Convert a string to an integer
 */
int atoi_n(const char *buf, size_t len) {
    int sign = 1;
    int result = 0;
    size_t i = 0;

    // skip leading spaces
    while (i < len && isspace((unsigned char)buf[i])) i++;

    // handle sign
    if (i < len && (buf[i] == '-' || buf[i] == '+')) {
        if (buf[i] == '-') sign = -1;
        i++;
    }

    // handle digits
    while (i < len && isdigit((unsigned char)buf[i])) {
        result = result * 10 + (buf[i] - '0');
        i++;
    }

    return result * sign;
}

/* ==================== API Handlers ==================== */

/**
 * @brief Parse multipart form data
 */
static int parse_multipart_data(const char *body, size_t body_len,
                                const char *boundary,
                                model_validation_config_t *config)
{
    if (!body || !boundary || !config)
    {
        printf("[DEBUG] Invalid parameters\r\n");
        return -1;
    }

    config->ai_image_data = NULL;
    config->ai_image_size = 0;
    config->draw_image_data = NULL;
    config->draw_image_size = 0;

    // Create boundary string with proper format
    char *boundary_str = (char *)buffer_calloc(1, strlen(boundary) + 3 + 1);
    memset(boundary_str, 0, strlen(boundary) + 3 + 1);
    if (!boundary_str) {
        return -1;
    }
    snprintf(boundary_str, strlen(boundary) + 3 + 1, "--%s", boundary);
    size_t boundary_len = strlen(boundary_str);
    printf("[DEBUG] boundary_str: %s\r\n", boundary_str);


    const char *current = body;
    const char *end = body + body_len;

    while (current < end)
    {
        // Find boundary
        const char *boundary_pos = memfind(current, body_len, boundary_str, boundary_len);
        if (!boundary_pos)
        {
            break;
        }

        current = boundary_pos + boundary_len;

        // Check if this is the closing boundary
        if (current + 2 <= end && strncmp(current, "--", 2) == 0)
        {
            break;
        }

        // Skip CRLF after boundary
        if (current < end && *current == '\r') current++;
        if (current < end && *current == '\n') current++;

        // Find header end
        const char *header_end = memfind(current, body_len, "\r\n\r\n", 4);
        if (!header_end)
        {
            break;
        }

        // Extract headers
        size_t header_len = header_end - current;
        char *header_buf = (char *)buffer_calloc(1, header_len + 2);
        memset(header_buf, 0, header_len + 2);
        if (!header_buf) {
            return -1;
        }
        memcpy(header_buf, current, header_len);
        header_buf[header_len] = '\0';
        printf("[DEBUG] header_buf: %s\r\n", header_buf);

        // Data starts after \r\n\r\n
        const char *data_start = header_end + 4;
        
        // Find the next boundary to determine data end
        const char *next_boundary = memfind(data_start, body_len, boundary_str, boundary_len);
        if (!next_boundary)
        {
            // Check if there's a closing boundary at the end
            const char *closing_boundary = memfind(data_start, body_len, "--", 2);
            if (closing_boundary && closing_boundary < end - 2) {
                next_boundary = closing_boundary;
            } else {
                next_boundary = end;
            }
        }

        size_t data_size = next_boundary - data_start;

        // Remove trailing CRLF from data
        if (data_size >= 2 && data_start[data_size - 2] == '\r' && data_start[data_size - 1] == '\n')
        {
            data_size -= 2;
        }

        // Check field name
        if (memfind(header_buf, header_len, "name=\"ai_image_width\"", 20))
        {
            config->ai_image_width = atoi_n(data_start, data_size);
            printf("[DEBUG] Found ai_image_width, value: %d\r\n", (int)config->ai_image_width);
        }
        else if (memfind(header_buf, header_len, "name=\"draw_image_width\"", 22))
        {
            config->draw_image_width = atoi_n(data_start, data_size);
            printf("[DEBUG] Found draw_image_width, value: %d\r\n", (int)config->draw_image_width);
        }
        else if (memfind(header_buf, header_len, "name=\"ai_image_height\"", 21))
        {
            config->ai_image_height = atoi_n(data_start, data_size);
            printf("[DEBUG] Found ai_image_height, value: %d\r\n", (int)config->ai_image_height);
        }
        else if (memfind(header_buf, header_len, "name=\"draw_image_height\"", 23))
        {
            config->draw_image_height = atoi_n(data_start, data_size);
            printf("[DEBUG] Found draw_image_height, value: %d\r\n", (int)config->draw_image_height);
        }
        else if (memfind(header_buf, header_len, "name=\"ai_image_quality\"", 23))
        {
            config->ai_image_quality = atoi_n(data_start, data_size);
            printf("[DEBUG] Found ai_image_quality, value: %d\r\n", (int)config->ai_image_quality);
        }
        else if (memfind(header_buf, header_len, "name=\"draw_image_quality\"", 25))
        {
            config->draw_image_quality = atoi_n(data_start, data_size);
            printf("[DEBUG] Found draw_image_quality, value: %d\r\n", (int)config->draw_image_quality);
        }
        else if (memfind(header_buf, header_len, "name=\"ai_image\"", 15))
        {
            printf("[DEBUG] Found ai_image, size: %d\r\n", (int)data_size);
            config->ai_image_data = (uint8_t *)data_start;
            config->ai_image_size = data_size;
        }
        else if (memfind(header_buf, header_len, "name=\"draw_image\"", 17))
        {
            printf("[DEBUG] Found draw_image, size: %d\r\n", (int)data_size);
            config->draw_image_data = (uint8_t *)data_start;
            config->draw_image_size = data_size;
        }

        buffer_free(header_buf);
        // Move to next boundary
        current = next_boundary;
    }

    LOG_SVC_INFO("[DEBUG] Final result: ai_image=%p (size=%d), draw_image=%p (size=%d) ",
            config->ai_image_data, (int)config->ai_image_size, config->draw_image_data, (int)config->draw_image_size);
    LOG_SVC_INFO("[DEBUG] ai_image_width=%d, ai_image_height=%d, ai_image_quality=%d, draw_image_width=%d, draw_image_height=%d, draw_image_quality=%d",
            (int)config->ai_image_width, (int)config->ai_image_height, (int)config->ai_image_quality, (int)config->draw_image_width, (int)config->draw_image_height, (int)config->draw_image_quality);
    
    buffer_free(boundary_str);

    // Return 0 if both images are found, or -1 if not
    if (config->ai_image_data && config->draw_image_data) {
        LOG_SVC_INFO("[DEBUG] Both images found successfully");
        return 0;
    } else {
        LOG_SVC_INFO("[DEBUG] Missing images: ai_image=%s, draw_image=%s",
               config->ai_image_data ? "found" : "missing",
               config->draw_image_data ? "found" : "missing");
        return -1;
    }
}

/**
 * @brief Handle model validation upload request
 * POST /api/v1/model/validation/upload
 */
static aicam_result_t model_validation_upload_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;
    
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    if (!web_api_verify_content_type(ctx, "multipart/form-data")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    LOG_SVC_INFO("Model validation upload request");

    model_validation_config_t config;
    memset(&config, 0, sizeof(model_validation_config_t));
    
    // Extract boundary from Content-Type header
    const char* content_type = ctx->request.content_type;
    printf("[DEBUG] Content-Type: \"%s\"\r\n", content_type);
    
    const char* boundary_start = memfind(content_type, strlen(content_type), "boundary=", 9);
    if (!boundary_start) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing boundary in Content-Type header");
    }
    boundary_start += 9; // Skip "boundary="
    
    // Parse multipart data
    LOG_SVC_INFO("[DEBUG] begin parse multipart data");
    
    int parse_result = parse_multipart_data(ctx->request.body, ctx->request.content_length, boundary_start, &config);
    
    if (parse_result != 0 || !config.ai_image_data || !config.draw_image_data) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Failed to parse multipart form data or missing required files");
    }
    
    LOG_SVC_INFO("Parsed multipart data: AI=%d bytes, Draw=%d bytes", config.ai_image_size, config.draw_image_size);
    
    // Initialize all pointers for proper cleanup
    char* encoded_data = NULL;
    cJSON* response_json = NULL;
    char* response_string = NULL;
    
    // Perform AI inference (data is already binary, no need to decode)
    ai_single_inference_result_t inference_result;
    aicam_result_t result = ai_single_image_inference(
        &config,
        &inference_result);
    
    if (result != AICAM_OK || !inference_result.success) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "AI inference failed");
    }
    
    // Encode output image to base64
    size_t output_b64_size = 0;
    encoded_data = base64_encode(inference_result.output_jpeg, inference_result.output_jpeg_size, &output_b64_size);
    
    if (!encoded_data) {
        ai_jpeg_free_buffer(inference_result.output_jpeg);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to encode output image");
    }
    
    // Create response JSON
    response_json = cJSON_CreateObject();
    if (!response_json) {
        buffer_free(encoded_data);
        ai_jpeg_free_buffer(inference_result.output_jpeg);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response");
    }
    
    // Add processing information
    cJSON_AddNumberToObject(response_json, "processing_time_ms", inference_result.processing_time_ms);
    cJSON_AddNumberToObject(response_json, "output_image_size", inference_result.output_jpeg_size);
    
    // Add AI result
    cJSON* ai_result_json = nn_create_ai_result_json(&inference_result.ai_result);
    if (ai_result_json) {
        cJSON_AddItemToObject(response_json, "ai_result", ai_result_json);
    }
    
    // Add output image
    cJSON_AddStringToObject(response_json, "output_image", encoded_data);
    
    // Send response
    response_string = cJSON_Print(response_json);
    LOG_SVC_INFO("response_string_len: %d", strlen(response_string));
    if (response_string) {
        api_response_success(ctx, response_string, "Model validation completed successfully");
        //buffer_free(response_string);
    } else {
        cJSON_Delete(response_json);
        buffer_free(encoded_data);
        ai_jpeg_free_buffer(inference_result.output_jpeg);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    // Clean up
    cJSON_Delete(response_json);
    buffer_free(encoded_data);
    ai_jpeg_free_buffer(inference_result.output_jpeg);
    
    LOG_SVC_INFO("Model validation upload completed successfully");
    return AICAM_OK;
}


/**
 * @brief Handle model reload request
 * POST /api/v1/model/reload
 */
static aicam_result_t model_reload_handler(http_handler_context_t* ctx) {
   if (!ctx) return AICAM_ERROR_INVALID_PARAM;
   
   if (!web_api_verify_method(ctx, "POST")) {
       return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
   }
   
   //reload the model
   aicam_result_t result = ai_reload_model();
   if (result != AICAM_OK) {
       return api_response_error(ctx, API_BUSINESS_ERROR_MODEL_RELOAD_FAILED, "Failed to reload model");
   }
   
   return api_response_success(ctx, NULL, "Model reload completed successfully");
}


/* ==================== API Route Registration ==================== */

// Model validation module routes
static const api_route_t model_validation_module_routes[] = {
    {
        .path = API_PATH_PREFIX "/model/validation/upload",
        .method = "POST",
        .handler = model_validation_upload_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/model/reload",
        .method = "POST",
        .handler = model_reload_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register model validation module
 */
aicam_result_t web_api_register_model_validation_module(void) {
    LOG_CORE_INFO("Registering model validation module");
    
    for (int i = 0; i < sizeof(model_validation_module_routes) / sizeof(model_validation_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&model_validation_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to register model validation module: %d", result);
            return result;
        }
    }
    
    LOG_CORE_INFO("Model validation module registered successfully");
    return AICAM_OK;
}
