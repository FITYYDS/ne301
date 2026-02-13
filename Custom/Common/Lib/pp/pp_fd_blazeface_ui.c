/**
******************************************************************************
* @file    pp_fd_blazeface_ui.c
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

#include "fd_blazeface_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for FD BlazeFace UI postprocess (face detector with keypoints) */
typedef struct {
    fd_blazeface_pp_static_param_t core;   /* original static params, now per-instance */
    fd_pp_outBuffer_t *fd_pp_buffer;      /* per-instance face detection buffer */
    fd_pp_keyPoints_t *fd_kp_buffer;      /* per-instance keypoints buffer */
    od_detect_t *od_detect_buffer;        /* per-instance OD-style detection buffer */
    char **class_names;                   /* per-instance class name array */
} pp_fd_blazeface_ui_params_t;

/*
Example JSON configuration (all fields optional):
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["face"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 100,
  "image_size": 256,
  "nb_detections_0": 896,
  "nb_detections_1": 896
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_fd_blazeface_ui_params_t *pp_ctx =
        (pp_fd_blazeface_ui_params_t *)hal_mem_alloc_any(sizeof(pp_fd_blazeface_ui_params_t));
    if (!pp_ctx) {
        return AI_FD_PP_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_fd_blazeface_ui_params_t));

    fd_blazeface_pp_static_param_t *params = &pp_ctx->core;

    /* Default values (self-contained, can be overridden by JSON) */
    params->in_size            = 256;   /* typical BlazeFace input size */
    params->nb_classes         = 1;
    params->nb_keypoints       = 6;
    params->nb_detections_0    = 896;   /* default number of priors for output 0 */
    params->nb_detections_1    = 896;   /* default number of priors for output 1 */
    params->max_boxes_limit    = 100;
    params->conf_threshold     = 0.6f;
    params->iou_threshold      = 0.5f;
    params->pAnchors_0         = NULL;  /* can be provided via JSON if needed */
    params->pAnchors_1         = NULL;
    params->nb_detect          = 0;

    /* Quantization parameters from NN instance (4 outputs) */
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

    /* Optional JSON overrides */
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
            }

            cJSON_Delete(root);
        }
    }

    /* Allocate per-instance buffers */
    size_t total_boxes = (size_t)params->nb_detections_0 + (size_t)params->nb_detections_1;
    size_t boxes_limit = MAX((size_t)params->max_boxes_limit, total_boxes);

    pp_ctx->fd_pp_buffer =
        (fd_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(fd_pp_outBuffer_t) * boxes_limit);
    pp_ctx->fd_kp_buffer =
        (fd_pp_keyPoints_t *)hal_mem_alloc_large(sizeof(fd_pp_keyPoints_t) *
                                                 boxes_limit * (size_t)params->nb_keypoints);
    pp_ctx->od_detect_buffer =
        (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);

    assert(pp_ctx->fd_pp_buffer != NULL &&
           pp_ctx->fd_kp_buffer != NULL &&
           pp_ctx->od_detect_buffer != NULL);

    /* Initialize keypoint pointers for FD output */
    for (size_t i = 0; i < boxes_limit; i++) {
        pp_ctx->fd_pp_buffer[i].pKeyPoints =
            &pp_ctx->fd_kp_buffer[i * (size_t)params->nb_keypoints];
    }

    fd_blazeface_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_FD_PP_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)pp_params;
    if (!pp) {
        return AI_FD_PP_ERROR_NO;
    }

    fd_blazeface_pp_static_param_t *params = &pp->core;
    (void)params;

    if (pp->fd_pp_buffer != NULL) {
        hal_mem_free(pp->fd_pp_buffer);
        pp->fd_pp_buffer = NULL;
    }
    if (pp->fd_kp_buffer != NULL) {
        hal_mem_free(pp->fd_kp_buffer);
        pp->fd_kp_buffer = NULL;
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

    hal_mem_free(pp);
    return AI_FD_PP_ERROR_NO;
}

static void fd_pp_out_t_to_pp_result_t(fd_pp_out_t *pFdOutput,
                                       pp_result_t *result,
                                       const pp_fd_blazeface_ui_params_t *pp)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pFdOutput->nb_detect > 0;
    result->od.nb_detect = (uint8_t)pFdOutput->nb_detect;
    result->od.detects = pp->od_detect_buffer;

    for (int i = 0; i < pFdOutput->nb_detect; i++) {
        float32_t x_center = pFdOutput->pOutBuff[i].x_center;
        float32_t y_center = pFdOutput->pOutBuff[i].y_center;
        float32_t width    = pFdOutput->pOutBuff[i].width;
        float32_t height   = pFdOutput->pOutBuff[i].height;

        result->od.detects[i].x =
            MAX(0.0f, MIN(1.0f, x_center - width / 2.0f));
        result->od.detects[i].y =
            MAX(0.0f, MIN(1.0f, y_center - height / 2.0f));
        result->od.detects[i].width =
            MAX(0.0f, MIN(1.0f, width));
        result->od.detects[i].height =
            MAX(0.0f, MIN(1.0f, height));
        result->od.detects[i].conf =
            MAX(0.0f, MIN(1.0f, pFdOutput->pOutBuff[i].conf));

        if (pFdOutput->pOutBuff[i].class_index >= 0 &&
            pFdOutput->pOutBuff[i].class_index < pp->core.nb_classes &&
            pp->class_names) {
            result->od.detects[i].class_name =
                pp->class_names[pFdOutput->pOutBuff[i].class_index];
        } else {
            result->od.detects[i].class_name = "face";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    (void)nn_inst;

    assert(nb_input == 4);
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)pp_params;
    fd_blazeface_pp_static_param_t *params = &pp->core;
    int32_t error = AI_FD_PP_ERROR_NO;
    params->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    fd_pp_out_t fd_out;
    fd_out.pOutBuff = pp->fd_pp_buffer;

    fd_blazeface_pp_in_t pp_input = {
        .pRawDetections_0 = (int8_t *)pInput[0],
        .pRawDetections_1 = (int8_t *)pInput[3],
        .pScores_0        = (int8_t *)pInput[1],
        .pScores_1        = (int8_t *)pInput[2],
    };

    error = fd_blazeface_pp_process_int8(&pp_input, &fd_out, params);
    if (error == AI_FD_PP_ERROR_NO) {
        fd_pp_out_t_to_pp_result_t(&fd_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_FD_PP_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_FD_PP_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_FD_PP_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_fd_blazeface_ui_params_t *pp = (pp_fd_blazeface_ui_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_FD_PP_ERROR_NO;
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

/* Static registration entry (FD BlazeFace with keypoints, mapped to OD type) */
const pp_entry_t pp_entry_fd_blazeface_ui = {
    .name = "pp_fd_blazeface_ui",
    .vt   = &vt
};

