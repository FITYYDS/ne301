/**
******************************************************************************
* @file    pp_sseg_deeplab_v3_ui.c
* @author  GPM Application Team
*
******************************************************************************
* @attention
*
* Copyright (c) 2023 STMicroelectronics.
* All rights reserved.
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
*/

#include "pp.h"
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include "cJSON.h"
#include "mem.h"
#include "ll_aton_runtime.h"
#include "ll_aton_reloc_network.h"

#include "sseg_deeplabv3_pp_if.h"

/* Per-instance parameters for SSEG DeepLabV3 UI postprocess */
typedef struct {
    sseg_deeplabv3_pp_static_param_t core;     /* original static params, now per-instance */
    uint8_t *sseg_class_map;                  /* per-instance class map buffer */
    char **class_names;                        /* per-instance class name array */
} pp_sseg_deeplab_v3_ui_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 21,
  "class_names": ["background", "aeroplane", "bicycle", "bird", "boat", ...],
  "width": 513,
  "height": 513
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_sseg_deeplab_v3_ui_params_t *pp_ctx = (pp_sseg_deeplab_v3_ui_params_t *)hal_mem_alloc_any(sizeof(pp_sseg_deeplab_v3_ui_params_t));
    if (!pp_ctx) {
        return AI_SSEG_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_sseg_deeplab_v3_ui_params_t));

    sseg_deeplabv3_pp_static_param_t *params = &pp_ctx->core;
    
    // Get quantization parameters from NN instance (for int8 models)
    // Note: DeepLabV3 int8 processing may not need explicit quantization params
    // as the library handles it internally

    params->nb_classes = 21;
    params->width = 513;
    params->height = 513;

    // If JSON is provided, parse and override parameters
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                // Compatibility: the input is already a postprocess_params object
                pp = root;
            }

            if (cJSON_IsObject(pp)) {
                cJSON *num_classes = cJSON_GetObjectItemCaseSensitive(pp, "num_classes");
                if (cJSON_IsNumber(num_classes)) {
                    params->nb_classes = (int32_t)num_classes->valuedouble;
                }

                cJSON *class_names = cJSON_GetObjectItemCaseSensitive(pp, "class_names");
                if (cJSON_IsArray(class_names)) {
                    pp_ctx->class_names = (char **)hal_mem_alloc_any(sizeof(char *) * params->nb_classes);
                    for (int i = 0; i < params->nb_classes; i++) {
                        cJSON *name = cJSON_GetArrayItem(class_names, i);
                        if (cJSON_IsString(name)) {
                            uint8_t len = strlen(name->valuestring) + 1;
                            pp_ctx->class_names[i] = (char *)hal_mem_alloc_any(sizeof(char) * len);
                            memcpy(pp_ctx->class_names[i], name->valuestring, len);
                        }
                    }
                }

                cJSON *width = cJSON_GetObjectItemCaseSensitive(pp, "width");
                if (cJSON_IsNumber(width)) {
                    params->width = (int32_t)width->valuedouble;
                }

                cJSON *height = cJSON_GetObjectItemCaseSensitive(pp, "height");
                if (cJSON_IsNumber(height)) {
                    params->height = (int32_t)height->valuedouble;
                }
            }

            cJSON_Delete(root);
        }
    }
    
    // Allocate per-instance class map buffer
    pp_ctx->sseg_class_map = (uint8_t *)hal_mem_alloc_large(sizeof(uint8_t) * params->width * params->height);
    assert(pp_ctx->sseg_class_map != NULL);
    
    sseg_deeplabv3_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_sseg_deeplab_v3_ui_params_t *pp = (pp_sseg_deeplab_v3_ui_params_t *)pp_params;
    if (!pp) {
        return AI_SSEG_POSTPROCESS_ERROR_NO;
    }

    sseg_deeplabv3_pp_static_param_t *params = &pp->core;
    
    if (pp->sseg_class_map != NULL) {
        hal_mem_free(pp->sseg_class_map);
        pp->sseg_class_map = NULL;
    }
    
    if (pp->class_names != NULL) {
        for (int i = 0; i < params->nb_classes; i++) {
            if (pp->class_names[i] != NULL) {
                hal_mem_free(pp->class_names[i]);
            }
        }
        hal_mem_free(pp->class_names);
        pp->class_names = NULL;
    }
    
    hal_mem_free(pp);
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static void sseg_pp_out_t_to_pp_result_t(sseg_pp_out_t *pSsegOutput,
                                         pp_result_t *result,
                                         const pp_sseg_deeplab_v3_ui_params_t *pp)
{
    result->type = PP_TYPE_SSEG;
    result->is_valid = (pSsegOutput->pOutBuff != NULL);
    result->sseg.class_map = pSsegOutput->pOutBuff;
    result->sseg.width = pp->core.width;
    result->sseg.height = pp->core.height;
    result->sseg.num_classes = pp->core.nb_classes;
    result->sseg.class_names = pp->class_names;
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    pp_sseg_deeplab_v3_ui_params_t *pp = (pp_sseg_deeplab_v3_ui_params_t *)pp_params;
    sseg_deeplabv3_pp_static_param_t *params = &pp->core;
    int32_t error = AI_SSEG_POSTPROCESS_ERROR_NO;

    memset(pResult, 0, sizeof(pp_result_t));

    sseg_pp_out_t sseg_pp_out;
    sseg_pp_out.pOutBuff = pp->sseg_class_map;
    
    // Note: DeepLabV3 UI uses float32 input but int8 processing function
    sseg_deeplabv3_pp_in_t pp_input = {
        .pRawData = (float32_t *)pInput[0],
    };
    
    // Use int8 processing function for int8 models
    error = sseg_deeplabv3_pp_process_int8(&pp_input, &sseg_pp_out, params);
    if (error == AI_SSEG_POSTPROCESS_ERROR_NO) {
        sseg_pp_out_t_to_pp_result_t(&sseg_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    // Semantic segmentation doesn't use confidence threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    (void)threshold;
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    // Semantic segmentation doesn't use NMS threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    (void)threshold;
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    // Semantic segmentation doesn't use confidence threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    *threshold = 0.0f;
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    // Semantic segmentation doesn't use NMS threshold
    // This is a no-op but kept for interface compatibility
    (void)params;
    *threshold = 0.0f;
    return AI_SSEG_POSTPROCESS_ERROR_NO;
}

static const pp_vtable_t vt = {
    .init = init,
    .run = run,
    .deinit = deinit,
    .set_confidence_threshold = set_confidence_threshold,
    .get_confidence_threshold = get_confidence_threshold,
    .set_nms_threshold = set_nms_threshold,
    .get_nms_threshold = get_nms_threshold,
};

// Define static registration entry
const pp_entry_t pp_entry_sseg_deeplab_v3_ui = {
    .name = "pp_sseg_deeplab_v3_ui",
    .vt = &vt
};

