/**
 * @file buffer_mgr.h
 * @brief Buffer Management System Header using mpool allocator
 * @details Static memory pool allocation and deallocation using mpool slab allocator
 */

#ifndef BUFFER_MGR_H
#define BUFFER_MGR_H

#include "aicam_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Memory Type Definitions ==================== */

/**
 * @brief Buffer memory type preferences for static allocation
 */
typedef enum {
    BUFFER_MEMORY_TYPE_ANY = 0,         // No preference, use any available
    BUFFER_MEMORY_TYPE_RAM,             // Prefer internal RAM (fast access)
    BUFFER_MEMORY_TYPE_PSRAM,           // Prefer external PSRAM (large capacity)
} buffer_memory_type_t;

/* ==================== System API ==================== */

/**
 * @brief Initialize buffer management system
 * @return aicam_result_t Operation result
 */
aicam_result_t buffer_mgr_init(void);

/**
 * @brief Deinitialize buffer management system
 * @return aicam_result_t Operation result
 */
aicam_result_t buffer_mgr_deinit(void);

/**
 * @brief Allocate memory and initialize to zero
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory
 */
void* buffer_calloc(size_t count, size_t size);

/**
 * @brief Free memory
 * @param ptr Pointer to free memory
 */
void buffer_free(void *ptr);

/**
 * @brief Allocate memory with preferred memory type
 * @param count Number of elements
 * @param size Size of each element
 * @param prefer_type Preferred memory type
 * @return Pointer to allocated memory
 */
void* buffer_calloc_ex(size_t count, size_t size, buffer_memory_type_t prefer_type);

/**
 * @brief Allocate aligned memory
 * @param size Size to allocate
 * @param alignment Alignment
 * @return Pointer to allocated memory
 */
void* buffer_malloc_aligned(size_t size, uint32_t alignment);

/**
 * @brief Allocate aligned memory with preferred memory type
 * @param size Size to allocate
 * @param alignment Alignment
 * @param prefer_type Preferred memory type
 * @return Pointer to allocated memory
 */
void* buffer_malloc_aligned_ex(size_t size, uint32_t alignment, buffer_memory_type_t prefer_type);


#ifdef __cplusplus
}
#endif

#endif // BUFFER_MGR_H 