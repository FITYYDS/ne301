/**
******************************************************************************
* @file    pp_od_yolo_v11_uf.c
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

#include "od_yolov8_pp_if.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Per-instance parameters for YOLOv11 UF postprocess (reuses YOLOv8 PP API) */
typedef struct {
    od_yolov8_pp_static_param_t core;   /* original static params, now per-instance */
    od_pp_outBuffer_t *od_pp_buffer;    /* per-instance output buffer */
    od_detect_t *od_detect_buffer;      /* per-instance detection buffer */
    char **class_names;                 /* per-instance class name array */
} pp_od_yolo_v11_uf_params_t;

/*
Example JSON configuration:
"postprocess_params": {
  "num_classes": 1,
  "class_names": ["person"],
  "confidence_threshold": 0.6,
  "iou_threshold": 0.5,
  "max_detections": 100,
  "total_boxes": 8400,
  "raw_output_scale": 0.003921569,
  "raw_output_zero_point": 0
}
*/
static int32_t init(const char *json_str, void **pp_params, void *nn_inst)
{
    pp_od_yolo_v11_uf_params_t *pp_ctx = (pp_od_yolo_v11_uf_params_t *)hal_mem_alloc_any(sizeof(pp_od_yolo_v11_uf_params_t));
    if (!pp_ctx) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }
    memset(pp_ctx, 0, sizeof(pp_od_yolo_v11_uf_params_t));

    od_yolov8_pp_static_param_t *params = &pp_ctx->core;

    // Set default values
    params->nb_classes = 1;
    params->nb_total_boxes = 8400;
    params->max_boxes_limit = 100;
    params->conf_threshold = 0.6f;
    params->iou_threshold = 0.5f;
    params->raw_output_scale = 1.0f;
    params->raw_output_zero_point = 0;
    params->nb_detect = 0;
    params->pScratchBuff = NULL;

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

                cJSON *total_boxes = cJSON_GetObjectItemCaseSensitive(pp, "total_boxes");
                if (cJSON_IsNumber(total_boxes)) {
                    params->nb_total_boxes = (int32_t)total_boxes->valuedouble;
                }

                cJSON *raw_scale = cJSON_GetObjectItemCaseSensitive(pp, "raw_output_scale");
                if (cJSON_IsNumber(raw_scale)) {
                    params->raw_output_scale = (float32_t)raw_scale->valuedouble;
                }

                cJSON *raw_zero = cJSON_GetObjectItemCaseSensitive(pp, "raw_output_zero_point");
                if (cJSON_IsNumber(raw_zero)) {
                    params->raw_output_zero_point = (int8_t)raw_zero->valuedouble;
                }
            }

            cJSON_Delete(root);
        }
    }

    // Allocate per-instance output buffers
    pp_ctx->od_pp_buffer = (od_pp_outBuffer_t *)hal_mem_alloc_large(sizeof(od_pp_outBuffer_t) * params->nb_total_boxes);
    pp_ctx->od_detect_buffer = (od_detect_t *)hal_mem_alloc_large(sizeof(od_detect_t) * params->max_boxes_limit);

    assert(pp_ctx->od_pp_buffer != NULL && pp_ctx->od_detect_buffer != NULL);

    od_yolov8_pp_reset(params);
    *pp_params = (void *)pp_ctx;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t deinit(void *pp_params)
{
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)pp_params;
    if (!pp) {
        return AI_OD_POSTPROCESS_ERROR_NO;
    }

    od_yolov8_pp_static_param_t *params = &pp->core;

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
                                       const pp_od_yolo_v11_uf_params_t *pp)
{
    result->type = PP_TYPE_OD;
    result->is_valid = pObjDetOutput->nb_detect > 0;
    result->od.nb_detect = pObjDetOutput->nb_detect;
    result->od.detects = pp->od_detect_buffer;

    // Convert detection format (YOLOv11 same as YOLOv8: centroid + normalized coords)
    for (int i = 0; i < pObjDetOutput->nb_detect; i++) {
        float x_center_norm = pObjDetOutput->pOutBuff[i].x_center;
        float y_center_norm = pObjDetOutput->pOutBuff[i].y_center;
        float width_norm = pObjDetOutput->pOutBuff[i].width;
        float height_norm = pObjDetOutput->pOutBuff[i].height;

        result->od.detects[i].x = MAX(0.0f, MIN(1.0f, x_center_norm - width_norm / 2.0f));
        result->od.detects[i].y = MAX(0.0f, MIN(1.0f, y_center_norm - height_norm / 2.0f));
        result->od.detects[i].width = MAX(0.0f, MIN(1.0f, width_norm));
        result->od.detects[i].height = MAX(0.0f, MIN(1.0f, height_norm));

        result->od.detects[i].conf = MAX(0.0f, MIN(1.0f, pObjDetOutput->pOutBuff[i].conf));

        if (pObjDetOutput->pOutBuff[i].class_index >= 0 &&
            pObjDetOutput->pOutBuff[i].class_index < pp->core.nb_classes &&
            pp->class_names) {
            result->od.detects[i].class_name = pp->class_names[pObjDetOutput->pOutBuff[i].class_index];
        } else {
            result->od.detects[i].class_name = "unknown";
        }
    }
}

static int32_t run(void *pInput[], uint32_t nb_input, void *pResult, void *pp_params, void *nn_inst)
{
    assert(nb_input == 1);
    int32_t error = AI_OD_POSTPROCESS_ERROR_NO;
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)pp_params;
    od_yolov8_pp_static_param_t *params = &pp->core;

    params->nb_detect = 0;
    memset(pResult, 0, sizeof(pp_result_t));

    od_pp_out_t od_pp_out;
    od_pp_out.pOutBuff = pp->od_pp_buffer;

    od_yolov8_pp_in_centroid_t pp_input = {
        .pRaw_detections = pInput[0],
    };

    /* YOLOv11 uses same output layout as YOLOv8; reuse ST YOLOv8 postprocess */
    error = od_yolov8_pp_process(&pp_input, &od_pp_out, params);
    if (error == AI_OD_POSTPROCESS_ERROR_NO) {
        od_pp_out_t_to_pp_result_t(&od_pp_out, (pp_result_t *)pResult, pp);
    }

    return error;
}

static int32_t set_confidence_threshold(void *params, float threshold)
{
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)params;
    pp->core.conf_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t set_nms_threshold(void *params, float threshold)
{
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)params;
    pp->core.iou_threshold = threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_confidence_threshold(void *params, float *threshold)
{
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)params;
    *threshold = pp->core.conf_threshold;
    return AI_OD_POSTPROCESS_ERROR_NO;
}

static int32_t get_nms_threshold(void *params, float *threshold)
{
    pp_od_yolo_v11_uf_params_t *pp = (pp_od_yolo_v11_uf_params_t *)params;
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

const pp_entry_t pp_entry_od_yolo_v11_uf = {
    .name = "pp_od_yolo_v11_uf",
    .vt = &vt
};
