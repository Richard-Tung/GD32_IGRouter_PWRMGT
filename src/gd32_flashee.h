/*
    Copyright 2026 Richard Tung <dyq@dyq.me>
*/

#include <Arduino.h>

// #define FLASH_EE_BASE_ADDR 0x08000000
#define FLASH_EE_BASE_ADDR FLASH_BASE

#define FLASH_EE_PAGE_SIZE 1024U

#define FLASH_EE_START_PAGE 60
#define FLASH_PAGE_USE_AS_EE 4  //64K Flash Use 60-63
#define FLASH_EE_FIRST_ADDR (FLASH_EE_BASE_ADDR + (FLASH_EE_PAGE_SIZE * FLASH_EE_START_PAGE))
#define FLASH_EE_LAST_ADDR (FLASH_EE_BASE_ADDR + FLASH_EE_PAGE_SIZE * (FLASH_EE_START_PAGE + FLASH_PAGE_USE_AS_EE - 1))

#define FLASH_EE_START_FLAG 0xAAAAAAAAU
#define FLASH_EE_END_FLAG 0xAAAAAAAAU
// #define FLASH_EE_ERASE_FLAG 0x00000000U

#define FLASH_EE_STORAGE_SIZE FLASH_EE_PAGE_SIZE/sizeof(uint32_t)-4

int8_t ee_init(uint32_t version);
int8_t ee_save();
bool ee_get(uint32_t offset,uint32_t &value);
bool ee_set(uint32_t offset,uint32_t value);
