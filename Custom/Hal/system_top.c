#include <string.h>
#include <stdio.h>
#include "Log/debug.h"
#include "Hal/mem.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "common_utils.h"
#include "mem.h"
#include "tx_api.h"
#include "lwip/stats.h"
#include "system_top.h"

// CPU load statistics (only when execution profile is enabled)
#if (defined(TX_ENABLE_EXECUTION_CHANGE_NOTIFY) || defined(TX_EXECUTION_PROFILE_ENABLE))
#define CPU_LOAD_HISTORY_DEPTH 8
typedef struct {
	ULONG64 current_total;
	ULONG64 current_thread_total;
	ULONG64 prev_total;
	ULONG64 prev_thread_total;
	struct {
		ULONG64 total;
		ULONG64 thread;
		uint32_t tick;
	} history[CPU_LOAD_HISTORY_DEPTH];
} cpuload_info_t;
static cpuload_info_t cpu_load = {0};
#endif

// TX Thread Status String List
const char *tx_thread_status_string[] = {
    "READY",
    "COMPLETED",
    "TERMINATED",
    "SUSPENDED",
    "SLEEP",
    "QUEUE_SUSP",
    "SEM_SUSP",
    "EVENT_FLAG",
    "BLOCK_MEMORY",
    "BYTE_MEMORY",
    "IO_DRIVER",
    "FILE",
    "TCP_IP",
    "MUTEX_SUSP",
    "PRO_CHANGE",
};
#define TX_THREAD_STATUS_STRING_COUNT (sizeof(tx_thread_status_string) / sizeof(tx_thread_status_string[0]))

// Common resources for thread statistics
static osSemaphoreId_t printf_sem = NULL;
static uint8_t top_tread_stack[1024 * 4] ALIGN_32;
const osThreadAttr_t topTask_attributes = {
    .name = "topTask",
    .priority = (osPriority_t) osPriorityRealtime7,
    .stack_mem = top_tread_stack,
    .stack_size = sizeof(top_tread_stack),
};

#if (defined(TX_ENABLE_EXECUTION_CHANGE_NOTIFY) || defined(TX_EXECUTION_PROFILE_ENABLE))
static void cpuload_update(cpuload_info_t *cpu_load)
{
	int i;

	cpu_load->history[1] = cpu_load->history[0];

	EXECUTION_TIME thread_total;
	EXECUTION_TIME isr;
	EXECUTION_TIME idle;

	_tx_execution_thread_total_time_get(&thread_total);
	_tx_execution_isr_time_get(&isr);
	_tx_execution_idle_time_get(&idle);

	cpu_load->history[0].total = thread_total + isr + idle;
	cpu_load->history[0].thread = thread_total;
	cpu_load->history[0].tick = HAL_GetTick();

	if (cpu_load->history[1].tick - cpu_load->history[2].tick < 1000)
		return ;

	for (i = 0; i < CPU_LOAD_HISTORY_DEPTH - 2; i++)
		cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i] = cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i - 1];
}
#endif

#if (defined(TX_ENABLE_EXECUTION_CHANGE_NOTIFY) || defined(TX_EXECUTION_PROFILE_ENABLE))
static void cpuload_get_info(cpuload_info_t *cpu_load, float *cpu_load_last, float *cpu_load_last_second,
                             float *cpu_load_last_five_seconds)
{
	ULONG64 total_diff, thread_diff;
	
	if (cpu_load_last) {
		total_diff = cpu_load->history[0].total - cpu_load->history[1].total;
		thread_diff = cpu_load->history[0].thread - cpu_load->history[1].thread;
		if (total_diff > 0)
			*cpu_load_last = 100.0 * thread_diff / total_diff;
		else
			*cpu_load_last = 0.0;
	}
	if (cpu_load_last_second) {
		total_diff = cpu_load->history[2].total - cpu_load->history[3].total;
		thread_diff = cpu_load->history[2].thread - cpu_load->history[3].thread;
		if (total_diff > 0)
			*cpu_load_last_second = 100.0 * thread_diff / total_diff;
		else
			*cpu_load_last_second = 0.0;
	}
	if (cpu_load_last_five_seconds) {
		total_diff = cpu_load->history[2].total - cpu_load->history[7].total;
		thread_diff = cpu_load->history[2].thread - cpu_load->history[7].thread;
		if (total_diff > 0)
			*cpu_load_last_five_seconds = 100.0 * thread_diff / total_diff;
		else
			*cpu_load_last_five_seconds = 0.0;
	}
}
#endif

// Note: _tx_thread_created_ptr and _tx_thread_created_count are ThreadX internal variables
extern TX_THREAD *_tx_thread_created_ptr;
extern ULONG _tx_thread_created_count;
extern TX_BYTE_POOL *_tx_byte_pool_created_ptr;
extern ULONG _tx_byte_pool_created_count;

static void system_top_printf_info(void)
{
    uint32_t total_size = 0, used_size = 0;
    TX_THREAD *tptr = _tx_thread_created_ptr;
    ULONG tcount = _tx_thread_created_count;
    TX_BYTE_POOL *bptr = _tx_byte_pool_created_ptr;
    ULONG bcount = _tx_byte_pool_created_count;
    ULONG i = 0;

#if defined(TX_THREAD_ENABLE_PERFORMANCE_INFO)
    TX_INTERRUPT_SAVE_AREA
    ULONG64 all_run_count = 0, *run_count_list = NULL;
    
    run_count_list = hal_mem_alloc(tcount * sizeof(ULONG64), MEM_FAST);
    if (run_count_list == NULL) return;
#endif

    LOG_SIMPLE("\r\n=================================================================================================================");
    
#if (defined(TX_ENABLE_EXECUTION_CHANGE_NOTIFY) || defined(TX_EXECUTION_PROFILE_ENABLE))
    float cpu_load_one_second;
    ULONG64 run_milliseconds = 0;
    
    cpuload_get_info(&cpu_load, NULL, &cpu_load_one_second, NULL);
    /* Convert from cycles to milliseconds (assuming 800MHz clock) */
    run_milliseconds = cpu_load.history[0].total / 800 / 1000;
    LOG_SIMPLE("CPU Load: %.2f%%", cpu_load_one_second);
    LOG_SIMPLE("TX Thread Total Time: %02lu:%02lu:%02lu.%03lu", (uint32_t)(run_milliseconds / 1000 / 60 / 60), (uint32_t)((run_milliseconds / 1000 / 60) % 60), (uint32_t)((run_milliseconds / 1000) % 60), (uint32_t)(run_milliseconds % 1000));
#endif
    LOG_SIMPLE("TX Thread Count: %lu", tcount);

    for (i = 0; i < bcount; i++) {
        if (bptr == NULL) break;
        LOG_SIMPLE("TX Pool[%s]: %lu / %lu bytes (%.2f%%)", bptr->tx_byte_pool_name, (bptr->tx_byte_pool_size - bptr->tx_byte_pool_available), bptr->tx_byte_pool_size, (float)(bptr->tx_byte_pool_size - bptr->tx_byte_pool_available) * 100.0f / (float)bptr->tx_byte_pool_size);
        bptr = bptr->tx_byte_pool_created_next;
    }

    if (hal_mem_get_info(&total_size, &used_size, MEM_FAST) == 0)
        LOG_SIMPLE("Hal Fast Memory: %lu / %lu bytes (%.2f%%)", used_size, total_size, (float)used_size * 100.0f / (float)total_size);
    if (hal_mem_get_info(&total_size, &used_size, MEM_LARGE) == 0)
        LOG_SIMPLE("Hal Large Memory: %lu / %lu bytes (%.2f%%)", used_size, total_size, (float)used_size * 100.0f / (float)total_size);

#if (LWIP_STATS && MEM_STATS)
    LOG_SIMPLE("LWIP Heap Memory: %lu / %lu bytes (%.2f%%)", lwip_stats.mem.used, lwip_stats.mem.used + lwip_stats.mem.avail, (float)lwip_stats.mem.used * 100.0f / (float)(lwip_stats.mem.used + lwip_stats.mem.avail));
#endif

    LOG_SIMPLE("-----------------------------------------------------------------------------------------------------------------");
    LOG_SIMPLE(" %2s | %28s | %12s | %4s | %10s | %14s | %14s | %6s", "ID", "Thread Name", "State", "Prio", "StackSize" , "CurStack", "MaxStack", "Ratio");
    LOG_SIMPLE("-----------------------------------------------------------------------------------------------------------------");
    
#if defined(TX_THREAD_ENABLE_PERFORMANCE_INFO)
    TX_DISABLE
    for (i = 0; i < tcount; i++) {
        if (tptr == NULL) break;
      	run_count_list[i] = tptr->tx_thread_run_count;
        tptr->tx_thread_run_count = 0;
      	all_run_count += run_count_list[i];
		tptr = tptr->tx_thread_created_next;
    }
    TX_RESTORE

	tptr = _tx_thread_created_ptr;
    for (i = 0; i < tcount; i++) {
        if (tptr == NULL) break;
#if defined(TX_ENABLE_STACK_CHECKING)
    	LOG_SIMPLE(" %2lu | %28s | %12s | %4u | %10u | %-6u %6.2f%% | %-6u %6.2f%% | %6.2f%%",
                   i,
                   tptr->tx_thread_name,
                   tx_thread_status_string[tptr->tx_thread_state],
                   tptr->tx_thread_priority,
                   tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_highest_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_highest_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   ((float)run_count_list[i]) * 100.0 / ((float)all_run_count));
#else
    	LOG_SIMPLE(" %2lu | %28s | %12s | %4u | %10u | %-6u %6.2f%% | %14s | %6.2f%%",
                   i,
                   tptr->tx_thread_name,
                   tx_thread_status_string[tptr->tx_thread_state],
                   tptr->tx_thread_priority,
                   tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   "N/A",
                   ((float)run_count_list[i]) * 100.0 / ((float)all_run_count));	
#endif
        tptr = tptr->tx_thread_created_next;
    }
    
    hal_mem_free(run_count_list);
#else
	tptr = _tx_thread_created_ptr;
    for (i = 0; i < tcount; i++) {
        if (tptr == NULL) break;
#if defined(TX_ENABLE_STACK_CHECKING)
    	LOG_SIMPLE(" %2lu | %28s | %12s | %4u | %10u | %-6u %6.2f%% | %-6u %6.2f%% | %6s",
                   i,
                   tptr->tx_thread_name,
                   tx_thread_status_string[tptr->tx_thread_state],
                   tptr->tx_thread_priority,
                   tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_highest_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_highest_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   "N/A");
#else
    	LOG_SIMPLE(" %2lu | %28s | %12s | %4u | %10u | %-6u %6.2f%% | %14s | %6s",
                   i,
                   tptr->tx_thread_name,
                   tx_thread_status_string[tptr->tx_thread_state],
                   tptr->tx_thread_priority,
                   tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr,
                   (float)(tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr) * 100.0 / (float)tptr->tx_thread_stack_size,
                   "N/A",
                   "N/A");
#endif
        tptr = tptr->tx_thread_created_next;
    }
#endif
    
    LOG_SIMPLE("=================================================================================================================\r\n");
}

static void system_top_process(void *argument)
{
    osStatus_t ret;

    while (1) {
#if (defined(TX_ENABLE_EXECUTION_CHANGE_NOTIFY) || defined(TX_EXECUTION_PROFILE_ENABLE))
        cpuload_update(&cpu_load);
#endif
        ret = osSemaphoreAcquire(printf_sem, 100);
        if (ret == osOK) system_top_printf_info();
    }
}

int system_top_cmd_deal(int argc, char* argv[])
{
    osSemaphoreRelease(printf_sem);
    return 0;
}

debug_cmd_reg_t system_top_cmd_table[] = {
    {"top",    "print system task information.",      system_top_cmd_deal},
};

static void system_top_cmd_register(void)
{
    printf_sem = osSemaphoreNew(1, 0, NULL);
    if (printf_sem == NULL) return;
    osThreadNew(system_top_process, NULL, &topTask_attributes);
    debug_cmdline_register(system_top_cmd_table, sizeof(system_top_cmd_table) / sizeof(system_top_cmd_table[0]));
}

void system_top_register(void)
{
    driver_cmd_register_callback("top", system_top_cmd_register);
}
