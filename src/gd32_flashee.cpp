/*
    Copyright 2026 Richard Tung <dyq@dyq.me>
*/

#include <Arduino.h>
#include "gd32_flashee.h"

struct FlashPart {
    uint32_t ee_start_flag;
    uint32_t version;
    uint32_t checksum;
    uint32_t storage[(FLASH_EE_PAGE_SIZE/sizeof(uint32_t))-4];
    uint32_t ee_end_flag;
};//4B START FLAG + 4B Version + 4B CHECKSUM, 4B END FLAG

static uint32_t cur_ee_offset_page = 0;// need add FLASH_EE_START_PAGE
static FlashPart ee_buffer;
// static uint32_t ee_length = FLASH_EE_PAGE_SIZE - 4 * 4;

uint32_t _ee_checksum(FlashPart& flash)
{
    uint32_t checksum=flash.version;
    for(int i=0;i<sizeof(flash.storage)/sizeof(uint32_t);i++)
    {
        checksum+=flash.storage[i];
    }
    return checksum;
}

bool _ee_load_from_flash(uint32_t version)
{
    // if(ee_offset<0 || ee_offset>FLASH_PAGE_USE_AS_EE) return false;
    for(cur_ee_offset_page=0;cur_ee_offset_page<FLASH_PAGE_USE_AS_EE;cur_ee_offset_page++)
    {
        memcpy_P((void*)(&ee_buffer),(void*)(FLASH_EE_BASE_ADDR + FLASH_EE_PAGE_SIZE * (FLASH_EE_START_PAGE + cur_ee_offset_page)), FLASH_EE_PAGE_SIZE);
        if(ee_buffer.ee_start_flag != FLASH_EE_START_FLAG || ee_buffer.ee_end_flag != FLASH_EE_END_FLAG ||
            ee_buffer.version!= version || _ee_checksum(ee_buffer)!=ee_buffer.checksum) continue;
        return true;
    }
    return false;
}

bool _ee_write_to_flash()
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t target_ee_page_offset=(cur_ee_offset_page+1)%FLASH_PAGE_USE_AS_EE;
    fmc_unlock();
    uint32_t flash_target_address = FLASH_EE_BASE_ADDR+FLASH_EE_PAGE_SIZE*(FLASH_EE_START_PAGE+target_ee_page_offset);
    uint32_t flash_previous_address = FLASH_EE_BASE_ADDR+FLASH_EE_PAGE_SIZE*(FLASH_EE_START_PAGE+cur_ee_offset_page);
    for(uint32_t offset=0;offset<FLASH_EE_PAGE_SIZE;offset+=sizeof(uint32_t))
    {
        if(*(uint32_t*)(flash_target_address + offset) != 0xFFFFFFFF)
        {
            fmc_page_erase(flash_target_address);
            break;
        }
    }
    uint32_t* data = (uint32_t*)&ee_buffer;
    for(int offset=0;offset<sizeof(FlashPart)/sizeof(uint32_t);offset++)
    {
        fmc_word_program(flash_target_address+offset*4,data[offset]);
    }
    fmc_page_erase(flash_previous_address);
    //fmc_word_program(flash_previous_address,FLASH_EE_ERASE_FLAG);
    fmc_lock();
    cur_ee_offset_page=target_ee_page_offset;
    __set_PRIMASK(primask);
    __enable_irq();
    return true;
}

bool _ee_update_checksum()
{
    ee_buffer.checksum=_ee_checksum(ee_buffer);
    return true;
}

int8_t ee_init(uint32_t version)
{
    // return: current page or -1: read failed
    if (!_ee_load_from_flash(version))
    {
        for(int i=0;i<sizeof(ee_buffer.storage)/sizeof(uint32_t);i++)
        {
            ee_buffer.storage[i]=0xff;
        }
        ee_buffer.ee_start_flag=FLASH_EE_START_FLAG;
        ee_buffer.ee_end_flag=FLASH_EE_END_FLAG;
        ee_buffer.version=version;
        _ee_update_checksum();
        return -1;
    }
    return cur_ee_offset_page;
}

int8_t ee_save()
{
    // return: current page or -1: save failed
    _ee_update_checksum();
    bool result=_ee_write_to_flash();
    return result ? cur_ee_offset_page: -1;
}

bool ee_get(uint32_t offset,uint32_t &value)
{
    if(offset>=(FLASH_EE_PAGE_SIZE/sizeof(uint32_t)-4)) return false;
    value=ee_buffer.storage[offset];
    return true;
}

bool ee_set(uint32_t offset,uint32_t value)
{
    if(offset>=(FLASH_EE_PAGE_SIZE/sizeof(uint32_t)-4)) return false;
    ee_buffer.storage[offset]=value;
    return true;
}
