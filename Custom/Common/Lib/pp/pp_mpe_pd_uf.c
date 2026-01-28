/**
******************************************************************************
* @file    pp_mpe_pd_uf.c
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

#include "pd_model_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for Palm Detector UF postprocess */
typedef struct {
    pd_model_pp_static_param_t core;     /* original static params, now per-instance */
    pd_pp_box_t *pd_pp_buffer;          /* per-instance pd buffer */
    pd_pp_point_t *pd_keypoints_buffer; /* per-instance keypoints buffer */
    mpe_detect_t *mpe_detect_buffer;    /* per-instance mpe detect buffer */
    char **class_names;                 /* per-instance class name array */
    pd_pp_point_t *anchors;             /* per-instance anchors */
    char **kp_names;                    /* per-instance keypoint names */
    uint8_t *keypoint_connections;      /* per-instance keypoint connections */
    uint8_t num_connections;            /* per-instance number of connections */
} pp_mpe_pd_uf_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["palm"],
  "confidence_threshold": 0.5,
  "iou_threshold": 0.3,
  "max_detections": 10,
  "image_width": 256,
  "image_height": 256,
  "num_keypoints": 7,
  "total_detections": 896,
  "anchors": [[x1, y1], [x2, y2], ...],
  "keypoint_names": ["wrist", "thumb_cmc", "thumb_mcp", "thumb_ip", "thumb_tip", "index_mcp", "index_tip"],
  "keypoint_connections": [
    [0, 1], [1, 2], [2, 3], [3, 4],
    [0, 5], [5, 6]
  ]
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_mpe_pd_uf_params_t *pp_ctx = (pp_mpe_pd_uf_params_t *)hal_mem_alloc_any(sizeof(pp_mpe_pd_uf_params_t));
    if (!pp_ctx) {
        return AI_PD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_mpe_pd_uf_params_t));

    pd_model_pp_static_param_t *params = &pp_ctx->core;

    params->width = 256;
    params->height = 256;
    params->nb_keypoints = 7;
    params->nb_total_boxes = 896;
    params->max_boxes_limit = 10;
    params->conf_threshold = 0.5f;
    params->iou_threshold = 0.3f;
    params->boxe_scale = 0.0f;
    params->proba_scale = 0.0f;
    params->boxe_zp = 0;
    params->proba_zp = 0;

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
                cJSON *class_names = cJSON_GetObjectItemCaseSensitive(pp, "class_names");
                if (cJSON_IsArray(class_names)) {
                    int num_classes = cJSON_GetArraySize(class_names);
                    if (num_classes > 0) {
                        pp_ctx->class_names = (char **)hal_mem_alloc_any(sizeof(char *) * num_classes);
                        for (int i = 0; i < num_classes; i++) {
                            cJSON *name = cJSON_GetArrayItem(class_names, i);
                            if (cJSON_IsString(name)) {
                                uint8_t len = strlen(name->valuestring) + 1;
                                pp_ctx->class_names[i] = (char *)hal_mem_alloc_any(sizeof(char) * len);
                                memcpy(pp_ctx->class_names[i], name->valuestring, len);
                            }
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

                cJSON *img_w = cJSON_GetObjectItemCaseSensitive(pp, "image_width");
                if (cJSON_IsNumber(img_w)) {
                    params->width = (uint32_t)img_w->valuedouble;
                }

                cJSON *img_h = cJSON_GetObjectItemCaseSensitive(pp, "image_height");
                if (cJSON_IsNumber(img_h)) {
                    params->height = (uint32_t)img_h->valuedouble;
                }

                cJSON *num_kp = cJSON_GetObjectItemCaseSensitive(pp, "num_keypoints");
                if (cJSON_IsNumber(num_kp)) {
                    params->nb_keypoints = (uint32_t)num_kp->valuedouble;
                }

                cJSON *total_det = cJSON_GetObjectItemCaseSensitive(pp, "total_detections");
                if (cJSON_IsNumber(total_det)) {
                    params->nb_total_boxes = (uint32_t)total_det->valuedouble;
                }

                // Parse anchors (array of [x, y] pairs)
                cJSON *anchors_array = cJSON_GetObjectItemCaseSensitive(pp, "anchors");
                if (cJSON_IsArray(anchors_array)) {
                    int count = cJSON_GetArraySize(anchors_array);
                    if (count > 0) {
                        pp_ctx->anchors = (pd_pp_point_t *)hal_mem_alloc_any(sizeof(pd_pp_point_t) * (size_t)count);
                        if (pp_ctx->anchors != NULL) {
                            for (int k = 0; k < count; ++k) {
                                cJSON *anchor_pair = cJSON_GetArrayItem(anchors_array, k);
                                if (cJSON_IsArray(anchor_pair) && cJSON_GetArraySize(anchor_pair) >= 2) {
                                    pp_ctx->anchors[k].x = (float32_t)cJSON_GetArrayItem(anchor_pair, 0)->valuedouble;
                                    pp_ctx->anchors[k].y = (float32_t)cJSON_GetArrayItem(anchor_pair, 1)->valuedouble;
                                }
                            }
                            params->pAnchors = pp_ctx->anchors;
                        }
                    }
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
    
    // Validate anchors are set
    if (params->pAnchors == NULL) {
        // Anchors must be provided via JSON configuration
        // Return a negative value to indicate error
        return -1;
    }
    
    // Allocate per-instance output buffers
    pp_ctx->pd_pp_buffer = (pd_pp_box_t *)hal_mem_alloc_large(sizeof(pd_pp_box_t) * params->max_boxes_limit);
    pp_ctx->pd_keypoints_buffer = (pd_pp_point_t *)hal_mem_alloc_large(sizeof(pd_pp_point_t) * params->max_boxes_limit * params->nb_keypoints);
    pp_ctx->mpe_detect_buffer = (mpe_detect_t *)hal_mem_alloc_large(sizeof(mpe_detect_t) * params->max_boxes_limit);
    
    assert(pp_ctx->pd_pp_buffer != NULL && pp_ctx->pd_keypoints_buffer != NULL && pp_ctx->mpe_detect_buffer != NULL);
    
    // Initialize keypoints pointers
    for (size_t i = 0; i < params->max_boxes_limit; i++) {
        pp_ctx->pd_pp_buffer[i].pKps = &pp_ctx->pd_keypoints_buffer[i * params->nb_keypoints];
    }
    
    pd_model_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_PD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)pp_params;
    if (!pp) {
        return AI_PD_POSTPROCESS_ERROR_NO;
    }

    pd_model_pp_static_param_t *params = &pp->core;
    
    if (pp->pd_pp_buffer != NULL) {
        hal_mem_free(pp->pd_pp_buffer);
        pp->pd_pp_buffer = NULL;
    }
    
    if (pp->pd_keypoints_buffer != NULL) {
        hal_mem_free(pp->pd_keypoints_buffer);
        pp->pd_keypoints_buffer = NULL;
    }
    
    if (pp->mpe_detect_buffer != NULL) {
        hal_mem_free(pp->mpe_detect_buffer);
        pp->mpe_detect_buffer = NULL;
    }
    
    if (pp->class_names != NULL) {
        // Free class names
        int num_classes = 1;  // Palm detector typically has 1 class
        for (int i = 0; i < num_classes; i++) {
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
    
    if (pp->anchors != NULL) {
        hal_mem_free(pp->anchors);
        pp->anchors = NULL;
    }
    
    hal_mem_free(pp);
    return AI_PD_POSTPROCESS_ERROR_NO;
}

static void pd_pp_out_t_to_pp_result_t(pd_pp_out_t *pPdOutput,
                                       pp_result_t *result,
                                       const pp_mpe_pd_uf_params_t *pp)
{
    result->type = PP_TYPE_MPE;  // Use MPE type as Palm Detector outputs similar format
    result->is_valid = pPdOutput->box_nb > 0;
    result->mpe.nb_detect = (uint8_t)pPdOutput->box_nb;
    result->mpe.detects = pp->mpe_detect_buffer;
    
    // Convert detection format
    for (int i = 0; i < pPdOutput->box_nb; i++) {
        // Ensure coordinates are within bounds
        result->mpe.detects[i].x = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].x_center - pPdOutput->pOutData[i].width / 2.0f));
        result->mpe.detects[i].y = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].y_center - pPdOutput->pOutData[i].height / 2.0f));
        result->mpe.detects[i].width = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].width));
        result->mpe.detects[i].height = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].height));
        result->mpe.detects[i].conf = pPdOutput->pOutData[i].prob;
        result->mpe.detects[i].class_name = (pp->class_names != NULL && pp->class_names[0] != NULL) ? pp->class_names[0] : "palm";
        
        // Convert keypoints
        for (int j = 0; j < pp->core.nb_keypoints && j < 33; j++) {
            if (pPdOutput->pOutData[i].pKps != NULL) {
                result->mpe.detects[i].keypoints[j].x = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].pKps[j].x));
                result->mpe.detects[i].keypoints[j].y = MAX(0.0f, MIN(1.0f, pPdOutput->pOutData[i].pKps[j].y));
                result->mpe.detects[i].keypoints[j].conf = 1.0f;  // Palm keypoints don't have confidence
            }
        }
        result->mpe.detects[i].nb_keypoints = pp->core.nb_keypoints;
        result->mpe.detects[i].num_connections = pp->num_connections;
        result->mpe.detects[i].keypoint_connections = pp->keypoint_connections;
        result->mpe.detects[i].keypoint_names = pp->kp_names;
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 2);
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)pp_params;
    pd_model_pp_static_param_t *params = &pp->core;
    int32_t error = AI_PD_POSTPROCESS_ERROR_NO;

    memset(pResult, 0, sizeof(pp_result_t));

    pd_pp_out_t pd_pp_out;
    pd_pp_out.pOutData = pp->pd_pp_buffer;
    
    // Palm Detector expects: probs, boxes
    pd_model_pp_in_t pp_input = {
        .pProbs = (float32_t *)pInput[0],
        .pBoxes = (float32_t *)pInput[1],
    };
    
    error = pd_model_pp_process(&pp_input, &pd_pp_out, params);
    if (error == AI_PD_POSTPROCESS_ERROR_NO) {
        pd_pp_out_t_to_pp_result_t(&pd_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_PD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_PD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_PD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_mpe_pd_uf_params_t *pp = (pp_mpe_pd_uf_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_PD_POSTPROCESS_ERROR_NO;
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
const pp_entry_t pp_entry_mpe_pd_uf = {
    .name = "pp_mpe_pd_uf",
    .vt = &vt
};

