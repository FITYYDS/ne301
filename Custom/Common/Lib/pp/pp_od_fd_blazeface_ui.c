/**
******************************************************************************
* @file    pp_od_fd_blazeface_ui.c
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

#include "od_fd_blazeface_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for BlazeFace UI postprocess */
typedef struct {
    od_fd_blazeface_pp_static_param_t core;     /* original static params, now per-instance */
    od_pp_outBuffer_t *od_pp_buffer;            /* per-instance output buffer */
    od_detect_t *od_detect_buffer;              /* per-instance detection buffer */
    char **class_names;                         /* per-instance class name array */
    float32_t *anchors_0;                       /* per-instance anchors_0 */
    float32_t *anchors_1;                       /* per-instance anchors_1 */
} pp_od_fd_blazeface_ui_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["face"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.3,
  "max_detections": 10,
  "image_size": 128,
  "num_keypoints": 6,
  "detections_0": 512,
  "detections_1": 384,
  "anchors_0": [x1, y1, w1, h1, x2, y2, w2, h2, ...],
  "anchors_1": [x1, y1, w1, h1, x2, y2, w2, h2, ...]
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_od_fd_blazeface_ui_params_t *pp_ctx = (pp_od_fd_blazeface_ui_params_t *)hal_mem_alloc_any(sizeof(pp_od_fd_blazeface_ui_params_t));
    if (!pp_ctx) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_od_fd_blazeface_ui_params_t));

    od_fd_blazeface_pp_static_param_t *params = &pp_ctx->core;
    
    // Get quantization parameters from NN instance (for int8 models)
    NN_Instance_TypeDef *NN_Instance = (NN_Instance_TypeDef *)nn_inst;
    if (NN_Instance != NULL) {
        const LL_Buffer_InfoTypeDef *buffers_info = ll_aton_reloc_get_output_buffers_info(NN_Instance, 0);
        if (buffers_info != NULL) {
            // BlazeFace has 4 outputs: scores_0, scores_1, detections_0, detections_1
            if (buffers_info[0].scale != NULL) {
                params->proba_0_scale = *(buffers_info[0].scale);
                params->proba_0_zero_point = *(buffers_info[0].offset);
            }
            if (buffers_info[1].scale != NULL) {
                params->proba_1_scale = *(buffers_info[1].scale);
                params->proba_1_zero_point = *(buffers_info[1].offset);
            }
            if (buffers_info[2].scale != NULL) {
                params->boxe_0_scale = *(buffers_info[2].scale);
                params->boxe_0_zero_point = *(buffers_info[2].offset);
            }
            if (buffers_info[3].scale != NULL) {
                params->boxe_1_scale = *(buffers_info[3].scale);
                params->boxe_1_zero_point = *(buffers_info[3].offset);
            }
        }
    }

    params->in_size = 128;
    params->nb_classes = 1;
    params->nb_keypoints = 6;
    params->nb_detections_0 = 512;
    params->nb_detections_1 = 384;
    params->max_boxes_limit = 10;
    params->conf_threshold = 0.6f;
    params->iou_threshold = 0.3f;
    params->nb_detect = 0;

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

                cJSON *conf_th = cJSON_GetObjectItemCaseSensitive(pp, "confidence_threshold");
                if (cJSON_IsNumber(conf_th)) {
                    params->conf_threshold = (float32_t)conf_th->valuedouble;
                }

                cJSON *iou_th = cJSON_GetObjectItemCaseSensitive(pp, "iou_threshold");
                if (cJSON_IsNumber(iou_th)) {
                    params->iou_threshold = (float32_t)iou_th->valuedouble;
                }

                cJSON *max_det = cJSON_GetObjectItemCaseSensitive(pp, "max_detections");
                if (cJSON_IsNumber(max_det)) {
                    params->max_boxes_limit = (int32_t)max_det->valuedouble;
                }

                cJSON *img_size = cJSON_GetObjectItemCaseSensitive(pp, "image_size");
                if (cJSON_IsNumber(img_size)) {
                    params->in_size = (int32_t)img_size->valuedouble;
                }

                cJSON *num_kp = cJSON_GetObjectItemCaseSensitive(pp, "num_keypoints");
                if (cJSON_IsNumber(num_kp)) {
                    params->nb_keypoints = (int32_t)num_kp->valuedouble;
                }

                cJSON *det_0 = cJSON_GetObjectItemCaseSensitive(pp, "detections_0");
                if (cJSON_IsNumber(det_0)) {
                    params->nb_detections_0 = (int32_t)det_0->valuedouble;
                }

                cJSON *det_1 = cJSON_GetObjectItemCaseSensitive(pp, "detections_1");
                if (cJSON_IsNumber(det_1)) {
                    params->nb_detections_1 = (int32_t)det_1->valuedouble;
                }

                // Parse anchors_0
                cJSON *anchors_0_array = cJSON_GetObjectItemCaseSensitive(pp, "anchors_0");
                if (cJSON_IsArray(anchors_0_array)) {
                    int count = cJSON_GetArraySize(anchors_0_array);
                    if (count > 0) {
                        pp_ctx->anchors_0 = (float32_t *)hal_mem_alloc_any(sizeof(float32_t) * (size_t)count);
                        if (pp_ctx->anchors_0 != NULL) {
                            for (int k = 0; k < count; ++k) {
                                cJSON *v = cJSON_GetArrayItem(anchors_0_array, k);
                                pp_ctx->anchors_0[k] = cJSON_IsNumber(v) ? (float32_t)v->valuedouble : 0.0f;
                            }
                            params->pAnchors_0 = pp_ctx->anchors_0;
                        }
                    }
                }

                // Parse anchors_1
                cJSON *anchors_1_array = cJSON_GetObjectItemCaseSensitive(pp, "anchors_1");
                if (cJSON_IsArray(anchors_1_array)) {
                    int count = cJSON_GetArraySize(anchors_1_array);
                    if (count > 0) {
                        pp_ctx->anchors_1 = (float32_t *)hal_mem_alloc_any(sizeof(float32_t) * (size_t)count);
                        if (pp_ctx->anchors_1 != NULL) {
                            for (int k = 0; k < count; ++k) {
                                cJSON *v = cJSON_GetArrayItem(anchors_1_array, k);
                                pp_ctx->anchors_1[k] = cJSON_IsNumber(v) ? (float32_t)v->valuedouble : 0.0f;
                            }
                            params->pAnchors_1 = pp_ctx->anchors_1;
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }
    }
    
    // Validate anchors are set
    if (params->pAnchors_0 == NULL || params->pAnchors_1 == NULL) {
        // Anchors must be provided via JSON configuration
        // Return a negative value to indicate error
        return -1;
    }
    
    // Allocate per-instance output buffers
    size_t boxes_limit = MAX(params->max_boxes_limit, params->nb_detections_0 + params->nb_detections_1);
    pp_ctx->od_pp_buffer = (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * boxes_limit);
    pp_ctx->od_detect_buffer = (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);
    assert(pp_ctx->od_pp_buffer != NULL && pp_ctx->od_detect_buffer != NULL);
    
    od_fd_blazeface_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)pp_params;
    if (!pp) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }

    od_fd_blazeface_pp_static_param_t *params = &pp->core;

    if (pp->od_pp_buffer != NULL) {
        hal_mem_free(pp->od_pp_buffer);
        pp->od_pp_buffer = NULL;
    }
    if (pp->od_detect_buffer != NULL) {
        hal_mem_free(pp->od_detect_buffer);
        pp->od_detect_buffer = NULL;
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
    if (pp->anchors_0 != NULL) {
        hal_mem_free(pp->anchors_0);
        pp->anchors_0 = NULL;
    }
    if (pp->anchors_1 != NULL) {
        hal_mem_free(pp->anchors_1);
        pp->anchors_1 = NULL;
    }

    hal_mem_free(pp);
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static void od_pp_out_t_to_pp_result_t(od_pp_out_t *pObjDetOutput,
                                       pp_result_t *result,
                                       const pp_od_fd_blazeface_ui_params_t *pp)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pObjDetOutput->nb_detect > 0;
    result->od.nb_detect = pObjDetOutput->nb_detect;
    result->od.detects = pp->od_detect_buffer;
    for (int i = 0; i < pObjDetOutput->nb_detect; i++) {
        result->od.detects[i].x = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].x_center - pObjDetOutput->pOutBuff[i].width / 2.0f));
        result->od.detects[i].y = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].y_center - pObjDetOutput->pOutBuff[i].height / 2.0f));
        result->od.detects[i].width = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].width));
        result->od.detects[i].height = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].height));
        result->od.detects[i].conf = pObjDetOutput->pOutBuff[i].conf;
        if (pObjDetOutput->pOutBuff[i].class_index >= 0 &&
            pObjDetOutput->pOutBuff[i].class_index < pp->core.nb_classes &&
            pp->class_names) {
            result->od.detects[i].class_name = pp->class_names[pObjDetOutput->pOutBuff[i].class_index];
        } else {
            result->od.detects[i].class_name = "face";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 4);
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)pp_params;
    od_fd_blazeface_pp_static_param_t *params = &pp->core;
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    params->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = pp->od_pp_buffer;
    
    // BlazeFace expects: scores_0, scores_1, detections_0, detections_1 (int8 for UI version)
    od_fd_blazeface_pp_in_t pp_input = {
        .pScores_0 = (int8_t *)pInput[0],
        .pScores_1 = (int8_t *)pInput[1],
        .pRawDetections_0 = (int8_t *)pInput[2],
        .pRawDetections_1 = (int8_t *)pInput[3],
    };
    
    // Use int8 processing function for int8 models
    error = od_fd_blazeface_pp_process_int8(&pp_input, &od_pp_out, params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_od_fd_blazeface_ui_params_t *pp = (pp_od_fd_blazeface_ui_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
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
const pp_entry_t pp_entry_od_fd_blazeface_ui = {
    .name = "pp_od_fd_blazeface_ui",
    .vt = &vt
};

