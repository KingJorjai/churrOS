/*
 * loader.h
 * Program Loader - Replaces the Process Generator
 */

#ifndef CHURROS_LOADER_H
#define CHURROS_LOADER_H

#include <stdint.h>
#include "pcb.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Program segment information */
typedef struct {
    uint32_t text_start;   /* Virtual address where .text segment starts */
    uint32_t text_size;    /* Size of .text segment in bytes */
    uint32_t data_start;   /* Virtual address where .data segment starts */
    uint32_t data_size;    /* Size of .data segment in bytes */
} ProgramInfo;

/* Load a program from file into memory and create PCB */
PCB* loader_load_program(const char* filename, PhysicalMemory* mem, uint32_t* next_pid);

/* Helper: Parse program file and get segment information */
int loader_parse_program(const char* filename, ProgramInfo* info, 
                         uint32_t** text_data, uint32_t** data_values);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_LOADER_H */
