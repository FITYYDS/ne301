/**
 * @file buffer_mgr.c
 * @brief Buffer Management System Implementation using ngx_slab allocator
 * @details Static memory pool allocation and deallocation from PSRAM and RAM
 * using the high-performance ngx_slab allocator.
 */

#include "buffer_mgr.h"
#include "mem.h"
#include "cJSON.h"


/* ==================== Internal Function Declarations ==================== */
static void* my_buffer_malloc(size_t size)
{
    return hal_mem_calloc_large(1, size);
}

static void my_buffer_free(void *ptr)
{
    hal_mem_free(ptr);
    ptr = NULL;
}

/**
 * @brief Initialize buffer management system
 * @return aicam_result_t Operation result
 */
aicam_result_t buffer_mgr_init(void)
{
    // init cJSON hooks to use buffer_mgr
    cJSON_Hooks hooks = {0};
    hooks.malloc_fn = my_buffer_malloc;
    hooks.free_fn = my_buffer_free;
    cJSON_InitHooks(&hooks);
    return AICAM_OK;
}

/*==============================================external functions==============================================*/

/**
 * @brief Deinitialize buffer management system
 * @return aicam_result_t Operation result
 */
aicam_result_t buffer_mgr_deinit(void)
{
    // do nothing for now
    return AICAM_OK;
}


 /**
  * @brief Frees a memory block allocated by this manager.
  */
 void buffer_free(void *ptr)
 {
     hal_mem_free(ptr);
     ptr = NULL;
 }
 
 /**
  * @brief Allocate memory with psram
  */
 void* buffer_calloc(size_t count, size_t size)
 {
     return hal_mem_calloc_large(count, size);
 }


 /**
  * @brief Allocate memory with preferred memory type
  */
 void* buffer_calloc_ex(size_t count, size_t size, buffer_memory_type_t prefer_type)
 {
    switch (prefer_type) {
        case BUFFER_MEMORY_TYPE_PSRAM:
            return hal_mem_calloc_large(count, size);
        case BUFFER_MEMORY_TYPE_RAM:
            return hal_mem_calloc_fast(count, size);
        default:
            return hal_mem_calloc_large(count, size);
    }

    return hal_mem_calloc_large(count, size);
 }
 
 /**
  * @brief Allocate aligned memory
  */
 void* buffer_malloc_aligned(size_t size, uint32_t alignment)
 {
    return hal_mem_alloc_aligned(size, alignment, MEM_LARGE);
 }

 /**
  * @brief Allocate aligned memory with preferred memory type
  */
 void* buffer_malloc_aligned_ex(size_t size, uint32_t alignment, buffer_memory_type_t prefer_type)
 {
    switch (prefer_type) {
        case BUFFER_MEMORY_TYPE_PSRAM:
            return hal_mem_alloc_aligned(size, alignment, MEM_LARGE);
        case BUFFER_MEMORY_TYPE_RAM:
            return hal_mem_alloc_aligned(size, alignment, MEM_FAST);
        default:
            return hal_mem_alloc_aligned(size, alignment, MEM_LARGE);
    }
    return hal_mem_alloc_aligned(size, alignment, MEM_LARGE);
 }
 
