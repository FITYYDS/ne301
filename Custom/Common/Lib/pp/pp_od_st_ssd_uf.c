/**
******************************************************************************
* @file    pp_od_st_ssd_uf.c
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

#include "od_ssd_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for SSD UF postprocess */
typedef struct {
    od_ssd_pp_static_param_t core;   /* original static params, now per-instance */
    od_pp_outBuffer_t *od_pp_buffer; /* per-instance output buffer */
    od_detect_t *od_detect_buffer;   /* per-instance detection buffer */
    char **class_names;              /* per-instance class name array */
} pp_od_st_ssd_uf_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 80,
  "class_names": ["person", "bicycle", "car", ...],
  "confidence_threshold": 0.5,
  "iou_threshold": 0.45,
  "max_detections": 100,
  "num_detections": 1917,
  "xy_inv_scale": 0.1,
  "wh_inv_scale": 0.2,
  "anchors": [ ... ]   // flat array matching SSD prior boxes layout
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    (void)nn_inst;

    pp_od_st_ssd_uf_params_t *pp_ctx =
        (pp_od_st_ssd_uf_params_t *)hal_mem_alloc_any(sizeof(pp_od_st_ssd_uf_params_t));
    if (!pp_ctx) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_od_st_ssd_uf_params_t));

    od_ssd_pp_static_param_t *params = &pp_ctx->core;

    /* Reasonable defaults (can be overridden by JSON) */
    params->nb_classes      = 80;
    params->nb_detections   = 1917;
    params->max_boxes_limit = 100;
    params->conf_threshold  = 0.5f;
    params->iou_threshold   = 0.45f;
    params->nb_detect       = 0;

    /* If JSON is provided, parse and override parameters */
    if (json_str != NULL) {
        cJSON *root = cJSON_Parse(json_str);
        if (root != NULL) {
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
            if (pp == NULL) {
                /* Compatibility: the input is already a postprocess_params object */
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

                cJSON *num_det = cJSON_GetObjectItemCaseSensitive(pp, "num_detections");
                if (cJSON_IsNumber(num_det)) {
                    params->nb_detections = (int32_t)num_det->valuedouble;
                }

                /* xy/wh scales and anchors, if any, are taken from library defaults */
            }

            cJSON_Delete(root);
        }
    }

    /* Allocate per-instance buffers */
    size_t boxes_limit = MAX(params->max_boxes_limit, params->nb_detections);
    pp_ctx->od_pp_buffer =
        (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * boxes_limit);
    pp_ctx->od_detect_buffer =
        (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);

    assert(pp_ctx->od_pp_buffer != NULL &&
           pp_ctx->od_detect_buffer != NULL);

    od_ssd_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)pp_params;
    if (!pp) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }

    od_ssd_pp_static_param_t *params = &pp->core;
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
                                       const pp_od_st_ssd_uf_params_t *pp)
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
            result->od.detects[i].class_name = "unknown";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    (void)nn_inst;

    assert(nb_input == 2);
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)pp_params;
    od_ssd_pp_static_param_t *params = &pp->core;
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    params->nb_detect = 0;

    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = pp->od_pp_buffer;

    od_ssd_pp_in_centroid_t pp_input = {
        .pBoxes  = pInput[0],
        .pScores = pInput[1],
    };

    error = od_ssd_pp_process(&pp_input, &od_pp_out, params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult, pp);
    }
    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_od_st_ssd_uf_params_t *pp = (pp_od_st_ssd_uf_params_t *)params;
    *threshold = pp->core.iou_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static const pp_vtable_t vt = {
    .init                   = init,
    .run                    = run,
    .deinit                 = deinit,
    .set_confidence_threshold = set_confidence_threshold,
    .get_confidence_threshold = get_confidence_threshold,
    .set_nms_threshold        = set_nms_threshold,
    .get_nms_threshold        = get_nms_threshold,
};

/* Static registration entry */
const pp_entry_t pp_entry_od_st_ssd_uf = {
    .name = "pp_od_st_ssd_uf",
    .vt   = &vt
};

