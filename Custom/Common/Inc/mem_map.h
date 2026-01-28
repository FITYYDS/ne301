#ifndef __MEMORY_MAP_H__
#define __MEMORY_MAP_H__

/*
=================== Memory Region Layout ===================
|   Region      |   Start Address   |   End Address     |   Size     |
|   APP         | 0x34000000        | 0x3419FFFF        | 1664K      |
| UNCACHED      | 0x341A0000        | 0x341FFFFF        | 384K       |
| AI            | 0x34200000        | 0x343BFFFF        | 1M + 768K  |
| AI_EXT        | 0x90000000        | 0x343BFFFF        | 8M         |
| APP EXT       | 0x90800000        | 0x91FFFFFF        | 24M        |

=================== Flash Partition Layout ===================
|   Partition    |   Start Address   |   End Address     |   Size     |
| FSBL           | 0x70000000        | 0x7007FFFF        | 512K       |
| NVS            | 0x70080000        | 0x7008FFFF        | 64K        |
| OTA            | 0x70090000        | 0x70091FFF        | 8K         |
| Reserve1       | 0x70093000        | 0x700FFFFF        | 440K       |
| APP1           | 0x70100000        | 0x704FFFFF        | 4M         |
| APP2           | 0x70500000        | 0x708FFFFF        | 4M         |
| AI_Default     | 0x70900000        | 0x70DFFFFF        | 5M         |
| AI_1           | 0x70E00000        | 0x712FFFFF        | 5M         |
| AI_2           | 0x71300000        | 0x717FFFFF        | 5M         |
| AI_3           | 0x71800000        | 0x71FFFFFF        | 8M         |
| LittleFS       | 0x72000000        | 0x75FFFFFF        | 64M        |
| Reserve2       | 0x76000000        | 0x77BFFFFFF       | 28M        |
| WiFi FW        | 0x77C00000        | 0x77FFFFFF        | 4M         |
*/


// =================== Memory Regions ===================
#define SRAM_BASE            0x34000000U
#ifdef BOOT_IN_PSRAM
#define SRAM_APP_BASE        0x90000000U
#define SRAM_APP_END         0x900FFFFFU
#else
#define SRAM_APP_BASE        0x34000000U
#define SRAM_APP_END         0x3419FFFFU
#endif
#define SRAM_APP_SIZE        (SRAM_APP_END - SRAM_APP_BASE)
#define SRAM_UNCACHED_BASE   0x341A0000U
#define SRAM_UNCACHED_END    0x341FFFFFU
#define SRAM_UNCACHED_SIZE   (0x34200000U - 0x341A0000U)   // 384K
#define SRAM_AI_BASE         0x34200000U
#define SRAM_AI_END          0x343BFFFFU
#define SRAM_AI_SIZE         (0x343C0000U - 0x34200000U)   // 1M + 768K
#define SRAM_AI_EXT_BASE     0x90000000U
#define SRAM_AI_EXT_END      0x907FFFFFU
#define SRAM_AI_EXT_SIZE     (0x90000000U - 0x907FFFFFU)   // 8M
#define SRAM_APP_EXT_BASE    0x90800000U
#define SRAM_APP_EXT_END     0x91FFFFFFU
#define SRAM_APP_EXT_SIZE    (0x92000000U - 0x90800000U)   // 24M

// =================== Flash Partitions ===================
#define FLASH_BASE      0x70000000U
#define FSBL_BASE       0x70000000U
#define FSBL_END        0x7007FFFFU
#define FSBL_SIZE       (0x70080000U - 0x70000000U)   // 512K
#define NVS_BASE        0x70080000U
#define NVS_END         0x7008FFFFU
#define NVS_SIZE        (0x70090000U - 0x70080000U)   // 64K
#define OTA_BASE        0x70090000U
#define OTA_END         0x70091FFFU
#define OTA_SIZE        (0x70092000U - 0x70090000U)   // 8K
#define RESERVE1_BASE   0x70093000U
#define RESERVE1_END    0x700FFFFFU
#define RESERVE1_SIZE   (0x70100000U - 0x70093000U)   // 440K
#define APP1_BASE       0x70100000U
#define APP1_END        0x704FFFFFU
#define APP1_SIZE       (0x70500000U - 0x70100000U)   // 4M
#define APP2_BASE       0x70500000U
#define APP2_END        0x708FFFFFU
#define APP2_SIZE       (0x70900000U - 0x70500000U)   // 4M
#define AI_DEFAULT_BASE 0x70900000U
#define AI_DEFAULT_END  0x70DFFFFFU
#define AI_DEFAULT_SIZE (0x70E00000U - 0x70900000U)   // 5M
#define AI_1_BASE       0x70E00000U
#define AI_1_END        0x712FFFFFU
#define AI_1_SIZE       (0x71300000U - 0x70E00000U)   // 5M
#define AI_2_BASE       0x71300000U
#define AI_2_END        0x717FFFFFU
#define AI_2_SIZE       (0x71800000U - 0x71300000U)   // 5M
#define AI_3_BASE       0x71800000U
#define AI_3_END        0x71FFFFFFU
#define AI_3_SIZE       (0x72000000U - 0x71800000U)   // 8M
#define LITTLEFS_BASE   0x72000000U
#define LITTLEFS_END    0x75FFFFFFU
#define LITTLEFS_SIZE   (0x76000000U - 0x72000000U)   // 64M
#define RESERVE2_BASE   0x76000000U
#define RESERVE2_END    0x77BFFFFFU
#define RESERVE2_SIZE   (0x77C00000U - 0x76000000U)   // 28M
#define WIFI_FW_BASE    0x77C00000U
#define WIFI_FW_END     0x77FFFFFFU
#define WIFI_FW_SIZE    (0x77FFFFFFU - 0x77C00000U)   // 4M

#endif // __MEMORY_MAP_H__