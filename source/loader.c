/*
 * loader.c
 * Program Loader - Reads programs from files and loads them into memory
 */

#include "../include/loader.h"
#include "../include/instruction.h"
#include "../include/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int loader_parse_program(const char* filename, ProgramInfo* info, 
                         uint32_t** text_data, uint32_t** data_values)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        LOG_ERROR(LOG_COMPONENT_LOADER, "Cannot open file %s", filename);
        return -1;
    }
    
    char line[256];
    int headers_read = 0; /* 0=none, 1=.text, 2=both */
    int in_code = 0;      /* Reading code section */
    uint32_t* text_arr = NULL;
    uint32_t* data_arr = NULL;
    uint32_t text_count = 0, data_count = 0;
    uint32_t text_capacity = 64, data_capacity = 64;
    
    /* Allocate initial arrays */
    text_arr = (uint32_t*)malloc(text_capacity * sizeof(uint32_t));
    data_arr = (uint32_t*)malloc(data_capacity * sizeof(uint32_t));
    
    if (!text_arr || !data_arr) {
        fclose(fp);
        free(text_arr);
        free(data_arr);
        return -1;
    }
    
    /* Prometheus format: .text header, .data header, then code, then data */
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = 0;
        
        /* Skip empty lines */
        if (strlen(line) == 0) continue;
        
        /* Check for segment markers */
        if (strncmp(line, ".text", 5) == 0) {
            sscanf(line, ".text %x", &info->text_start);
            headers_read = 1;
            continue;
        }
        
        if (strncmp(line, ".data", 5) == 0) {
            sscanf(line, ".data %x", &info->data_start);
            headers_read = 2;
            in_code = 1; /* After both headers, code section starts */
            continue;
        }
        
        /* Parse content: after both headers, code comes first, then data */
        if (headers_read == 2) {
            uint32_t value;
            if (sscanf(line, "%x", &value) == 1) {
                if (in_code) {
                    /* Add to code section */
                    if (text_count >= text_capacity) {
                        text_capacity *= 2;
                        text_arr = (uint32_t*)realloc(text_arr, text_capacity * sizeof(uint32_t));
                    }
                    text_arr[text_count++] = value;
                    
                    /* EXIT instruction marks end of code section */
                    if ((value & 0xF0000000) == 0xF0000000) {
                        in_code = 0; /* Switch to data section */
                    }
                } else {
                    /* Add to data section */
                    if (data_count >= data_capacity) {
                        data_capacity *= 2;
                        data_arr = (uint32_t*)realloc(data_arr, data_capacity * sizeof(uint32_t));
                    }
                    data_arr[data_count++] = value;
                }
            }
        }
    }
    
    fclose(fp);
    
    info->text_size = text_count * WORD_SIZE;
    info->data_size = data_count * WORD_SIZE;
    
    *text_data = text_arr;
    *data_values = data_arr;
    
    LOG_DEBUG(LOG_COMPONENT_LOADER, 
           "Parsed %s: .text at 0x%06X (%u bytes), .data at 0x%06X (%u bytes)",
           filename, info->text_start, info->text_size, info->data_start, info->data_size);
    
    return 0;
}

PCB* loader_load_program(const char* filename, PhysicalMemory* mem, uint32_t* next_pid)
{
    if (!filename || !mem || !next_pid) return NULL;
    
    ProgramInfo info;
    uint32_t* text_data = NULL;
    uint32_t* data_values = NULL;
    
    /* Parse the program file */
    if (loader_parse_program(filename, &info, &text_data, &data_values) != 0) {
        return NULL;
    }
    
    LOG_INFO(LOG_COMPONENT_LOADER, "Loading program %s (PID=%u)", filename, *next_pid);
    
    /* Create PCB */
    PCB* pcb = (PCB*)malloc(sizeof(PCB));
    if (!pcb) {
        free(text_data);
        free(data_values);
        return NULL;
    }
    
    pcb->pid = (*next_pid)++;
    pcb->ttl = 0;  /* Will be set during execution */
    pcb->state = PROCESS_STATE_NEW;
    pcb->cpu_id = -1;
    pcb->core_id = -1;
    pcb->hw_thread_id = -1;
    pcb->temperature = 0;
    pcb->ticks_since_swap = 0;
    
    /* Initialize memory map */
    pcb->mm.code_start = info.text_start;
    pcb->mm.data_start = info.data_start;
    pcb->mm.code_size = info.text_size;
    pcb->mm.data_size = info.data_size;
    
    /* Create page table */
    PageTable* pt = page_table_create(mem);
    if (!pt) {
        free(pcb);
        free(text_data);
        free(data_values);
        return NULL;
    }
    
    pcb->mm.pgb = pt->physical_address;
    
    /* Load code segment into memory */
    uint32_t text_words = info.text_size / WORD_SIZE;
    
    char line[256];
    snprintf(line, sizeof(line), "          │ Loading .text segment (%u words)\n", text_words);
    write(STDOUT_FILENO, line, strlen(line));
    for (uint32_t i = 0; i < text_words; i++) {
        uint32_t vaddr = info.text_start + i * WORD_SIZE;
        uint32_t vpn = GET_VPN(vaddr);
        uint32_t offset = GET_OFFSET(vaddr);
        
        /* Allocate frame if new page and not already mapped */
        uint32_t existing_pfn = page_table_lookup(pt, mem, vpn);
        if (existing_pfn == 0xFFFFFFFF) {
            /* Page not mapped yet, allocate new frame */
            uint32_t pfn = physical_memory_allocate_frame(mem);
            if (pfn == 0xFFFFFFFF) {
                LOG_ERROR(LOG_COMPONENT_LOADER, "Failed to allocate frame for .text");
                page_table_destroy(pt, mem);
                free(pcb);
                free(text_data);
                free(data_values);
                return NULL;
            }
            page_table_map(pt, mem, vpn, pfn);
        }
        
        /* Write instruction to physical memory */
        uint32_t pfn = page_table_lookup(pt, mem, vpn);
        uint32_t paddr = (pfn << PAGE_BITS) | offset;
        physical_memory_write_word(mem, paddr, text_data[i]);
    }
    
    /* Load data segment into memory */
    uint32_t data_words = info.data_size / WORD_SIZE;
    
    snprintf(line, sizeof(line), "          │ Loading .data segment (%u words)\n", data_words);
    write(STDOUT_FILENO, line, strlen(line));
    for (uint32_t i = 0; i < data_words; i++) {
        uint32_t vaddr = info.data_start + i * WORD_SIZE;
        uint32_t vpn = GET_VPN(vaddr);
        uint32_t offset = GET_OFFSET(vaddr);
        
        /* Allocate frame if new page and not already mapped */
        uint32_t existing_pfn = page_table_lookup(pt, mem, vpn);
        if (existing_pfn == 0xFFFFFFFF) {
            /* Page not mapped yet, allocate new frame */
            uint32_t pfn = physical_memory_allocate_frame(mem);
            if (pfn == 0xFFFFFFFF) {
                LOG_ERROR(LOG_COMPONENT_LOADER, "Failed to allocate frame for .data");
                page_table_destroy(pt, mem);
                free(pcb);
                free(text_data);
                free(data_values);
                return NULL;
            }
            page_table_map(pt, mem, vpn, pfn);
        }
        
        /* Write data to physical memory */
        uint32_t pfn = page_table_lookup(pt, mem, vpn);
        uint32_t paddr = (pfn << PAGE_BITS) | offset;
        physical_memory_write_word(mem, paddr, data_values[i]);
    }
    
    pcb->is_loaded = 1;
    
    LOG_NOTICE(LOG_COMPONENT_LOADER, 
           "Program loaded successfully (PID=%u, PTBR=0x%06X)", pcb->pid, pcb->mm.pgb);
    
    /* Print program disassembly */
    LOG_DEBUG(LOG_COMPONENT_LOADER, "Program disassembly:");
    
    /* Print .text section header */
    char section_line[256];
    snprintf(section_line, sizeof(section_line), "          │ .text @0x%06X\n", info.text_start);
    write(STDOUT_FILENO, section_line, strlen(section_line));
    
    for (uint32_t i = 0; i < text_words; i++) {
        instruction_print(text_data[i], info.text_start + i * WORD_SIZE, 1);
    }
    
    /* Print .data section header */
    snprintf(section_line, sizeof(section_line), "          │ .data @0x%06X\n", info.data_start);
    write(STDOUT_FILENO, section_line, strlen(section_line));
    
    LOG_DEBUG(LOG_COMPONENT_LOADER, "Data segment:");
    for (uint32_t i = 0; i < data_words; i++) {
        char line[256];
        snprintf(line, sizeof(line), "          │ 0x%06X: [%08X] %d\n",
                 info.data_start + i * WORD_SIZE, 
                 data_values[i], 
                 (int32_t)data_values[i]);
        write(STDOUT_FILENO, line, strlen(line));
    }
    
    /* Cleanup temporary arrays */
    free(text_data);
    free(data_values);
    free(pt);  /* Keep page table in memory, just free the local struct */
    
    return pcb;
}
