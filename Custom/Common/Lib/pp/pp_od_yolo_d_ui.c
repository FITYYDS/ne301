/**
******************************************************************************
* @file    pp_od_yolo_d_ui.c
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

#include "od_yolo_d_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for Yolo-d UI postprocess */
typedef struct {
    od_yolo_d_pp_static_param_t core;   /* original static params, now per-instance */
    od_pp_outBuffer_t *od_pp_buffer;    /* per-instance output buffer */
    od_detect_t *od_detect_buffer;      /* per-instance detection buffer */
    char **class_names;                 /* per-instance class name array */
    uint8_t strides[3];                 /* strides array storage */
} pp_od_yolo_d_ui_params_t;

/*
Example JSON configuration (all fields optional):
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["object"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 100,
  "img_width": 256,
  "img_height": 256,
  "strides": [8, 16, 32]
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_od_yolo_d_ui_params_t *pp_ctx =
        (pp_od_yolo_d_ui_params_t *)hal_mem_alloc_any(sizeof(pp_od_yolo_d_ui_params_t));
    if (!pp_ctx) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_od_yolo_d_ui_params_t));

    od_yolo_d_pp_static_param_t *params = &pp_ctx->core;

    /* Get quantization parameters from NN instance (for int8 models) */
    NN_Instance_TypeDef *NN_Instance = (NN_Instance_TypeDef *)nn_inst;
    if (NN_Instance != NULL) {
        const LL_Buffer_InfoTypeDef *buffers_info =
            ll_aton_reloc_get_output_buffers_info(NN_Instance, 0);
        if (buffers_info != NULL && buffers_info[0].scale != NULL) {
            params->raw_output_scale = *(buffers_info[0].scale);
            params->raw_output_zero_point = (int8_t)(*buffers_info[0].offset);
        }
    }

    /* Default values consistent with library macros (can be overridden by JSON) */
    params->nb_classes      = 1;
    params->height          = 256;
    params->width           = 256;
    params->max_boxes_limit = 100;
    params->conf_threshold  = 0.6f;
    params->iou_threshold   = 0.5f;
    params->nb_detect       = 0;

    pp_ctx->strides[0] = 8;
    pp_ctx->strides[1] = 16;
    pp_ctx->strides[2] = 32;
    params->strides    = pp_ctx->strides;
    params->strides_nb = 3;

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

                cJSON *img_w = cJSON_GetObjectItemCaseSensitive(pp, "img_width");
                if (cJSON_IsNumber(img_w)) {
                    params->width = (uint32_t)img_w->valuedouble;
                }

                cJSON *img_h = cJSON_GetObjectItemCaseSensitive(pp, "img_height");
                if (cJSON_IsNumber(img_h)) {
                    params->height = (uint32_t)img_h->valuedouble;
                }

                cJSON *strides = cJSON_GetObjectItemCaseSensitive(pp, "strides");
                if (cJSON_IsArray(strides)) {
                    int count = cJSON_GetArraySize(strides);
                    if (count > 0 && count <= 3) {
                        params->strides_nb = (uint8_t)count;
                        for (int i = 0; i < count; i++) {
                            cJSON *v = cJSON_GetArrayItem(strides, i);
                            pp_ctx->strides[i] =
                                cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : pp_ctx->strides[i];
                        }
                    }
                }
            }

            cJSON_Delete(root);
        }
    }

    /* Allocate per-instance buffers */
    /* Upper bound for candidate boxes is width*height/ (min stride^2) but we rely on max_boxes_limit */
    size_t boxes_limit = (size_t)params->max_boxes_limit;
    pp_ctx->od_pp_buffer =
        (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * boxes_limit);
    pp_ctx->od_detect_buffer =
        (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);

    assert(pp_ctx->od_pp_buffer != NULL && pp_ctx->od_detect_buffer != NULL);

    od_yolo_d_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)pp_params;
    if (!pp) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }

    od_yolo_d_pp_static_param_t *params = &pp->core;
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

    hal_mem_free(pp);
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static void od_pp_out_t_to_pp_result_t(od_pp_out_t *pObjDetOutput,
                                       pp_result_t *result,
                                       const pp_od_yolo_d_ui_params_t *pp)
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
            result->od.detects[i].class_name = "object";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    (void)nn_inst;

    assert(nb_input == 1);
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)pp_params;
    od_yolo_d_pp_static_param_t *params = &pp->core;
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    params->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = pp->od_pp_buffer;

    od_yolo_d_pp_in_centroid_t pp_input = {
        .pRaw_detections = pInput[0],
    };

    error = od_yolo_d_pp_process_int8(&pp_input, &od_pp_out, params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_od_yolo_d_ui_params_t *pp = (pp_od_yolo_d_ui_params_t *)params;
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
const pp_entry_t pp_entry_od_yolo_d_ui = {
    .name = "pp_od_yolo_d_ui",
    .vt   = &vt
};

