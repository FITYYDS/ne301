/**
******************************************************************************
* @file    pp_od_fd_blazeface_uu.c
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
#include <string.h>
#include "cJSON.h"
#include "mem.h"
#include "ll_aton_runtime.h"
#include "ll_aton_reloc_network.h"

#include "od_blazeface_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for BlazeFace UU postprocess (treated as OD) */
typedef struct {
    od_blazeface_pp_static_param_t core;  /* original static params, now per-instance */
    od_pp_outBuffer_t *od_pp_buffer;      /* per-instance output buffer */
    od_detect_t *od_detect_buffer;        /* per-instance detection buffer */
    char **class_names;                   /* per-instance class name array */
    float32_t *anchors_0;                 /* per-instance anchors for output 0 */
    float32_t *anchors_1;                 /* per-instance anchors for output 1 */
} pp_od_fd_blazeface_uu_params_t;

static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_od_fd_blazeface_uu_params_t *pp_ctx =
        (pp_od_fd_blazeface_uu_params_t *)hal_mem_alloc_any(sizeof(pp_od_fd_blazeface_uu_params_t));
    if (!pp_ctx) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_od_fd_blazeface_uu_params_t));

    od_blazeface_pp_static_param_t *params = &pp_ctx->core;

    /* Quantization information from NN instance (uint8 outputs, 4 tensors) */
    NN_Instance_TypeDef *NN_Instance = (NN_Instance_TypeDef *)nn_inst;
    if (NN_Instance != NULL) {
        const LL_Buffer_InfoTypeDef *buffers_info =
            ll_aton_reloc_get_output_buffers_info(NN_Instance, 0);
        if (buffers_info != NULL && buffers_info[0].scale != NULL) {
            params->boxe_0_scale      = *(buffers_info[0].scale);
            params->boxe_0_zero_point = (uint8_t)(*buffers_info[0].offset);
        }
        if (buffers_info != NULL && buffers_info[1].scale != NULL) {
            params->proba_0_scale      = *(buffers_info[1].scale);
            params->proba_0_zero_point = (uint8_t)(*buffers_info[1].offset);
        }
        if (buffers_info != NULL && buffers_info[2].scale != NULL) {
            params->proba_1_scale      = *(buffers_info[2].scale);
            params->proba_1_zero_point = (uint8_t)(*buffers_info[2].offset);
        }
        if (buffers_info != NULL && buffers_info[3].scale != NULL) {
            params->boxe_1_scale      = *(buffers_info[3].scale);
            params->boxe_1_zero_point = (uint8_t)(*buffers_info[3].offset);
        }
    }

    /* Default values similar to UF/UI variants */
    params->nb_classes      = 1;
    params->nb_keypoints    = 6;
    params->nb_detections_0 = 896;
    params->nb_detections_1 = 896;
    params->in_size         = 256;
    params->max_boxes_limit = 100;
    params->conf_threshold  = 0.6f;
    params->iou_threshold   = 0.5f;
    params->nb_detect       = 0;
    params->pAnchors_0      = NULL;
    params->pAnchors_1      = NULL;

    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                pp = root;
            }

            if (cJSON_IsObject(pp)) {
                cJSON *num_classes = cJSON_GetObjectItemCaseSensitive(pp, "num_classes");
                if (cJSON_IsNumber(num_classes)) {
                    params->nb_classes = (int32_t)num_classes->valuedouble;
                }

                cJSON *class_names = cJSON_GetObjectItemCaseSensitive(pp, "class_names");
                if (cJSON_IsArray(class_names)) {
                    pp_ctx->class_names =
                        (char **)hal_mem_alloc_any(sizeof(char *) * params->nb_classes);
                    for (int i = 0; i < params->nb_classes; i++) {
                        cJSON *name = cJSON_GetArrayItem(class_names, i);
                        if (cJSON_IsString(name)) {
                            uint8_t len = (uint8_t)strlen(name->valuestring) + 1U;
                            pp_ctx->class_names[i] =
                                (char *)hal_mem_alloc_any(sizeof(char) * len);
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
                    params->in_size = (uint32_t)img_size->valuedouble;
                }

                cJSON *nb_det0 = cJSON_GetObjectItemCaseSensitive(pp, "nb_detections_0");
                if (cJSON_IsNumber(nb_det0)) {
                    params->nb_detections_0 = (int32_t)nb_det0->valuedouble;
                }

                cJSON *nb_det1 = cJSON_GetObjectItemCaseSensitive(pp, "nb_detections_1");
                if (cJSON_IsNumber(nb_det1)) {
                    params->nb_detections_1 = (int32_t)nb_det1->valuedouble;
                }

                /* Optional anchors arrays */
                cJSON *anchors0 = cJSON_GetObjectItemCaseSensitive(pp, "anchors_0");
                if (cJSON_IsArray(anchors0)) {
                    int count = cJSON_GetArraySize(anchors0);
                    if (count > 0) {
                        pp_ctx->anchors_0 =
                            (float32_t *)hal_mem_alloc_any(sizeof(float32_t) * (size_t)count);
                        if (pp_ctx->anchors_0 != NULL) {
                            for (int k = 0; k < count; ++k) {
                                cJSON *v = cJSON_GetArrayItem(anchors0, k);
                                pp_ctx->anchors_0[k] =
                                    cJSON_IsNumber(v) ? (float32_t)v->valuedouble : 0.0f;
                            }
                            params->pAnchors_0 = pp_ctx->anchors_0;
                        }
                    }
                }

                cJSON *anchors1 = cJSON_GetObjectItemCaseSensitive(pp, "anchors_1");
                if (cJSON_IsArray(anchors1)) {
                    int count = cJSON_GetArraySize(anchors1);
                    if (count > 0) {
                        pp_ctx->anchors_1 =
                            (float32_t *)hal_mem_alloc_any(sizeof(float32_t) * (size_t)count);
                        if (pp_ctx->anchors_1 != NULL) {
                            for (int k = 0; k < count; ++k) {
                                cJSON *v = cJSON_GetArrayItem(anchors1, k);
                                pp_ctx->anchors_1[k] =
                                    cJSON_IsNumber(v) ? (float32_t)v->valuedouble : 0.0f;
                            }
                            params->pAnchors_1 = pp_ctx->anchors_1;
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }
    }

    size_t total_boxes = (size_t)params->nb_detections_0 + (size_t)params->nb_detections_1;
    size_t boxes_limit = MAX((size_t)params->max_boxes_limit, total_boxes);

    pp_ctx->od_pp_buffer =
        (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * boxes_limit);
    pp_ctx->od_detect_buffer =
        (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);

    assert(pp_ctx->od_pp_buffer != NULL && pp_ctx->od_detect_buffer != NULL);

    od_blazeface_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)pp_params;
    if (!pp) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }

    od_blazeface_pp_static_param_t *params = &pp->core;
    (void)params;

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
                                       const pp_od_fd_blazeface_uu_params_t *pp)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pObjDetOutput->nb_detect > 0;
    result->od.nb_detect = (uint8_t)pObjDetOutput->nb_detect;
    result->od.detects = pp->od_detect_buffer;

    for (int i = 0; i < pObjDetOutput->nb_detect; i++) {
        float32_t x_center = pObjDetOutput->pOutBuff[i].x_center;
        float32_t y_center = pObjDetOutput->pOutBuff[i].y_center;
        float32_t width    = pObjDetOutput->pOutBuff[i].width;
        float32_t height   = pObjDetOutput->pOutBuff[i].height;

        result->od.detects[i].x =
            MAX(0.0f, MIN(1.0f, x_center - width / 2.0f));
        result->od.detects[i].y =
            MAX(0.0f, MIN(1.0f, y_center - height / 2.0f));
        result->od.detects[i].width =
            MAX(0.0f, MIN(1.0f, width));
        result->od.detects[i].height =
            MAX(0.0f, MIN(1.0f, height));
        result->od.detects[i].conf =
            MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].conf));

        if (pObjDetOutput->pOutBuff[i].class_index >= 0 &&
            pObjDetOutput->pOutBuff[i].class_index < pp->core.nb_classes &&
            pp->class_names) {
            result->od.detects[i].class_name =
                pp->class_names[pObjDetOutput->pOutBuff[i].class_index];
        } else {
            result->od.detects[i].class_name = "face";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    (void)nn_inst;

    assert(nb_input == 4);
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)pp_params;
    od_blazeface_pp_static_param_t *params = &pp->core;
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    params->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = pp->od_pp_buffer;

    od_blazeface_pp_in_t pp_input = {
        .pRawDetections_0 = (uint8_t *)pInput[0],
        .pRawDetections_1 = (uint8_t *)pInput[3],
        .pScores_0        = (uint8_t *)pInput[1],
        .pScores_1        = (uint8_t *)pInput[2],
    };

    error = od_blazeface_pp_process_uint8(&pp_input, &od_pp_out, params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_od_fd_blazeface_uu_params_t *pp = (pp_od_fd_blazeface_uu_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static const pp_vtable_t vt = {
    .init                     = init,
    .run                      = run,
    .deinit                   = deinit,
    .set_confidence_threshold = set_confidence_threshold,
    .get_confidence_threshold = get_confidence_threshold,
    .set_nms_threshold        = set_nms_threshold,
    .get_nms_threshold        = get_nms_threshold,
};

/* Static registration entry */
const pp_entry_t pp_entry_od_blazeface_uu = {
    .name = "pp_od_blazeface_uu",
    .vt   = &vt
};

