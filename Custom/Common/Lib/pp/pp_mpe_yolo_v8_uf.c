/**
******************************************************************************
* @file    pp_mpe_yolo_v8_uf.c
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

#include "mpe_yolov8_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for MPE YOLOv8 UF postprocess */
typedef struct {
    mpe_yolov8_pp_static_param_t core;     /* original static params, now per-instance */
    mpe_pp_outBuffer_t *mpe_pp_buffer;    /* per-instance output buffer */
    mpe_pp_keyPoints_t *mpe_keypoints_buffer; /* per-instance keypoints buffer */
    mpe_detect_t *mpe_detect_buffer;      /* per-instance detection buffer */
    char **class_names;                    /* per-instance class name array */
    uint8_t *keypoint_connections;        /* per-instance keypoint connections */
    uint8_t num_connections;              /* per-instance number of connections */
    char **kp_names;                      /* per-instance keypoint names */
} pp_mpe_yolo_v8_uf_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["person"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 10,
  "num_keypoints": 17,
  "total_boxes": 1344,
  "raw_output_scale": 0.003921569,
  "raw_output_zero_point": 0,
  "keypoint_names": [
    "nose", "left_eye", "right_eye", "left_ear", "right_ear",
    "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
    "left_wrist", "right_wrist", "left_hip", "right_hip",
    "left_knee", "right_knee", "left_ankle", "right_ankle"
  ],
  "keypoint_connections": [
    [0, 1], [0, 2], [1, 3], [2, 4], [1, 2], [3, 5], [4, 6],
    [5, 6], [5, 7], [7, 9], [6, 8], [8, 10],
    [5, 11], [6, 12], [11, 12],
    [11, 13], [13, 15], [12, 14], [14, 16]
  ]
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_mpe_yolo_v8_uf_params_t *pp_ctx = (pp_mpe_yolo_v8_uf_params_t *)hal_mem_alloc_any(sizeof(pp_mpe_yolo_v8_uf_params_t));
    if (!pp_ctx) {
        return AI_MPE_PP_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_mpe_yolo_v8_uf_params_t));

    mpe_yolov8_pp_static_param_t *params = &pp_ctx->core;
    
    // Set default values
    params->nb_classes = 2;
    params->nb_total_boxes = 1344;
    params->max_boxes_limit = 10;
    params->conf_threshold = 0.6f;
    params->iou_threshold = 0.5f;
    params->nb_keypoints = 17;
    params->nb_detect = 0;
    
    // Parse JSON if provided
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                // Compatibility: input is postprocess_params object directly
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

                cJSON *num_keypoints = cJSON_GetObjectItemCaseSensitive(pp, "num_keypoints");
                if (cJSON_IsNumber(num_keypoints)) {
                    params->nb_keypoints = (uint32_t)num_keypoints->valuedouble;
                }

                // Parse keypoint names
                cJSON *kp_names = cJSON_GetObjectItemCaseSensitive(pp, "keypoint_names");
                if (cJSON_IsArray(kp_names)) {
                    if (params->nb_keypoints > 0) {
                        pp_ctx->kp_names = (char **)hal_mem_alloc_any(sizeof(char *) * params->nb_keypoints);
                        for (int i = 0; i < params->nb_keypoints; i++) {
                            cJSON *name = cJSON_GetArrayItem(kp_names, i);
                            if (cJSON_IsString(name)) {
                                uint8_t len = strlen(name->valuestring) + 1;
                                pp_ctx->kp_names[i] = (char *)hal_mem_alloc_any(len);
                                memcpy(pp_ctx->kp_names[i], name->valuestring, len);
                            } else {
                                pp_ctx->kp_names[i] = NULL;
                            }
                        }
                    }
                }

                cJSON *total_boxes = cJSON_GetObjectItemCaseSensitive(pp, "total_boxes");
                if (cJSON_IsNumber(total_boxes)) {
                    params->nb_total_boxes = (int32_t)total_boxes->valuedouble;
                }

                // Parse keypoint connections
                cJSON *connections = cJSON_GetObjectItemCaseSensitive(pp, "keypoint_connections");
                if (cJSON_IsArray(connections)) {
                    pp_ctx->num_connections = cJSON_GetArraySize(connections);
                    if (pp_ctx->num_connections > 0) {
                        pp_ctx->keypoint_connections = (uint8_t *)hal_mem_alloc_any(sizeof(uint8_t) * pp_ctx->num_connections * 2);
                        for (int i = 0; i < pp_ctx->num_connections; i++) {
                            cJSON *connection = cJSON_GetArrayItem(connections, i);
                            if (cJSON_IsArray(connection) && cJSON_GetArraySize(connection) == 2) {
                                pp_ctx->keypoint_connections[i * 2 + 0] = (uint8_t)cJSON_GetArrayItem(connection, 0)->valuedouble;
                                pp_ctx->keypoint_connections[i * 2 + 1] = (uint8_t)cJSON_GetArrayItem(connection, 1)->valuedouble;
                            }
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }
    }
    
    // Allocate per-instance output buffers
    pp_ctx->mpe_pp_buffer = (mpe_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(mpe_pp_outBuffer_t) * params->nb_total_boxes);
    pp_ctx->mpe_keypoints_buffer = (mpe_pp_keyPoints_t *)hal_mem_alloc_large(sizeof(mpe_pp_keyPoints_t) * params->nb_total_boxes * params->nb_keypoints);
    pp_ctx->mpe_detect_buffer = (mpe_detect_t *)hal_mem_alloc_large(sizeof(mpe_detect_t) * params->max_boxes_limit);
    
    assert(pp_ctx->mpe_pp_buffer != NULL && pp_ctx->mpe_keypoints_buffer != NULL && pp_ctx->mpe_detect_buffer != NULL);
    
    // Initialize keypoints pointers
    for (size_t i = 0; i < params->nb_total_boxes; i++) {
        pp_ctx->mpe_pp_buffer[i].pKeyPoints = &pp_ctx->mpe_keypoints_buffer[i * params->nb_keypoints];
    }
    
    mpe_yolov8_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)pp_params;
    if (!pp) {
        return AI_MPE_PP_ERROR_NO;
    }

    mpe_yolov8_pp_static_param_t *params = &pp->core;
    
    if (pp->mpe_pp_buffer != NULL) {
        hal_mem_free(pp->mpe_pp_buffer);
        pp->mpe_pp_buffer = NULL;
    }
    
    if (pp->mpe_keypoints_buffer != NULL) {
        hal_mem_free(pp->mpe_keypoints_buffer);
        pp->mpe_keypoints_buffer = NULL;
    }
    
    if (pp->mpe_detect_buffer != NULL) {
        hal_mem_free(pp->mpe_detect_buffer);
        pp->mpe_detect_buffer = NULL;
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
    
    if (pp->kp_names != NULL) {
        for (int i = 0; i < params->nb_keypoints; i++) {
            if (pp->kp_names[i] != NULL) {
                hal_mem_free(pp->kp_names[i]);
            }
        }
        hal_mem_free(pp->kp_names);
        pp->kp_names = NULL;
    }

    if (pp->keypoint_connections != NULL) {
        hal_mem_free(pp->keypoint_connections);
        pp->keypoint_connections = NULL;
        pp->num_connections = 0;
    }
    
    hal_mem_free(pp);
    return AI_MPE_PP_ERROR_NO;
}

static void mpe_pp_out_t_to_pp_result_t(mpe_pp_out_t *pMpeOutput,
                                        pp_result_t *result,
                                        const pp_mpe_yolo_v8_uf_params_t *pp)
{
    result->type = PP_TYPE_MPE;
    result->is_valid = pMpeOutput->nb_detect > 0;
    result->mpe.nb_detect = pMpeOutput->nb_detect;
    result->mpe.detects = pp->mpe_detect_buffer;
    // Convert detection format if needed
    for (int i = 0; i < pMpeOutput->nb_detect; i++) {
        // Ensure coordinates are within bounds
        result->mpe.detects[i].x = MAX(0.0f, pMpeOutput->pOutBuff[i].x_center - pMpeOutput->pOutBuff[i].width / 2.0f);
        result->mpe.detects[i].y = MAX(0.0f, pMpeOutput->pOutBuff[i].y_center - pMpeOutput->pOutBuff[i].height / 2.0f);
        result->mpe.detects[i].width = MIN(1.0f, pMpeOutput->pOutBuff[i].width);
        result->mpe.detects[i].height = MIN(1.0f, pMpeOutput->pOutBuff[i].height);
        result->mpe.detects[i].conf = pMpeOutput->pOutBuff[i].conf;
        if (pMpeOutput->pOutBuff[i].class_index >= 0 &&
            pMpeOutput->pOutBuff[i].class_index < pp->core.nb_classes &&
            pp->class_names) {
            result->mpe.detects[i].class_name = pp->class_names[pMpeOutput->pOutBuff[i].class_index];
        } else {
            result->mpe.detects[i].class_name = "unknown";
        }
        for (int j = 0; j < pp->core.nb_keypoints; j++) {
            result->mpe.detects[i].keypoints[j].x = MAX(0.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].x);
            result->mpe.detects[i].keypoints[j].y = MAX(0.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].y);
            result->mpe.detects[i].keypoints[j].conf = MIN(1.0f, pMpeOutput->pOutBuff[i].pKeyPoints[j].conf);
        }
        result->mpe.detects[i].nb_keypoints = pp->core.nb_keypoints;
        result->mpe.detects[i].num_connections = pp->num_connections;
        result->mpe.detects[i].keypoint_connections = pp->keypoint_connections;
        result->mpe.detects[i].keypoint_names = pp->kp_names;
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)pp_params;
    mpe_yolov8_pp_static_param_t *params = &pp->core;
    int32_t error = AI_MPE_PP_ERROR_NO;
    
    params->nb_detect = 0;
    memset(pResult, 0, sizeof(pp_result_t));

    mpe_pp_out_t mpe_pp_out;
    mpe_pp_out.pOutBuff = pp->mpe_pp_buffer;
    
    mpe_yolov8_pp_in_centroid_t pp_input = {
        .pRaw_detections = pInput[0],
    };
    
    error = mpe_yolov8_pp_process(&pp_input, &mpe_pp_out, params);
    if (error == AI_MPE_PP_ERROR_NO) {
        mpe_pp_out_t_to_pp_result_t(&mpe_pp_out, (pp_result_t *)pResult, pp);
    }
    
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_MPE_PP_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_mpe_yolo_v8_uf_params_t *pp = (pp_mpe_yolo_v8_uf_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_MPE_PP_ERROR_NO;
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
const pp_entry_t pp_entry_mpe_yolo_v8_uf = {
    .name = "pp_mpe_yolo_v8_uf",
    .vt = &vt
};
