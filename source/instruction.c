/*
 * instruction.c
 * Instruction set implementation and execution
 */

#include "../include/instruction.h"
#include "../include/logging.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int instruction_execute(Instruction instr, MMU* mmu, PhysicalMemory* mem)
{
    if (!mmu || !mem) return 0;
    
    uint32_t opcode = GET_OPCODE(instr);
    
    switch (opcode) {
        case OP_LD: {
            /* ld Rd, addr - Load word from memory to register */
            uint32_t rd = GET_RD(instr);
            uint32_t vaddr = GET_ADDR(instr);
            
            /* Translate virtual to physical address */
            uint32_t paddr = mmu_translate(mmu, mem, vaddr, 0);
            if (paddr == 0xFFFFFFFF) {
                LOG_ERROR(LOG_COMPONENT_INSTRUCTION, "LD failed: translation error for addr 0x%06X", vaddr);
                return 0;
            }
            
            /* Read from physical memory */
            uint32_t value = physical_memory_read_word(mem, paddr);
            mmu->registers[rd] = (int32_t)value;  /* Store as signed */
            
            LOG_DEBUG(LOG_COMPONENT_INSTRUCTION, "LD r%u, 0x%06X -> r%u = %d (0x%08X)", 
                     rd, vaddr, rd, mmu->registers[rd], value);
            
            break;
        }
        
        case OP_ST: {
            /* st Rs, addr - Store word from register to memory */
            uint32_t rs = GET_RD(instr);  /* Using RD field for source register */
            uint32_t vaddr = GET_ADDR(instr);
            
            /* Translate virtual to physical address */
            uint32_t paddr = mmu_translate(mmu, mem, vaddr, 1);
            if (paddr == 0xFFFFFFFF) {
                LOG_ERROR(LOG_COMPONENT_INSTRUCTION, "ST failed: translation error for addr 0x%06X", vaddr);
                return 0;
            }
            
            /* Write to physical memory */
            uint32_t value = (uint32_t)mmu->registers[rs];
            physical_memory_write_word(mem, paddr, value);
            
            LOG_DEBUG(LOG_COMPONENT_INSTRUCTION, "ST r%u, 0x%06X <- %d (0x%08X)", 
                     rs, vaddr, (int32_t)value, value);
            
            break;
        }
        
        case OP_ADD: {
            /* add Rd, Rs1, Rs2 - Add two registers */
            uint32_t rd = GET_RD(instr);
            uint32_t rs1 = GET_RS1(instr);
            uint32_t rs2 = GET_RS2(instr);
            
            int32_t val1 = mmu->registers[rs1];
            int32_t val2 = mmu->registers[rs2];
            mmu->registers[rd] = val1 + val2;
            
            LOG_DEBUG(LOG_COMPONENT_INSTRUCTION, "ADD r%u, r%u, r%u -> r%u = %d + %d = %d", 
                     rd, rs1, rs2, rd, val1, val2, mmu->registers[rd]);
            
            break;
        }
        
        case OP_EXIT: {
            /* exit - Terminate program */
            return 0;  /* Signal to stop execution */
        }
        
        default:
            LOG_ERROR(LOG_COMPONENT_INSTRUCTION, "Unknown opcode 0x%X in instruction 0x%08X", opcode, instr);
            return 0;
    }
    
    return 1;  /* Continue execution */
}

void instruction_print(Instruction instr, uint32_t addr, int is_continuation)
{
    uint32_t opcode = GET_OPCODE(instr);
    char buffer[128];
    
    switch (opcode) {
        case OP_LD: {
            uint32_t rd = GET_RD(instr);
            uint32_t vaddr = GET_ADDR(instr);
            snprintf(buffer, sizeof(buffer), "0x%06X: [%08X] ld r%u, 0x%06X", addr, instr, rd, vaddr);
            break;
        }
        
        case OP_ST: {
            uint32_t rs = GET_RD(instr);
            uint32_t vaddr = GET_ADDR(instr);
            snprintf(buffer, sizeof(buffer), "0x%06X: [%08X] st r%u, 0x%06X", addr, instr, rs, vaddr);
            break;
        }
        
        case OP_ADD: {
            uint32_t rd = GET_RD(instr);
            uint32_t rs1 = GET_RS1(instr);
            uint32_t rs2 = GET_RS2(instr);
            snprintf(buffer, sizeof(buffer), "0x%06X: [%08X] add r%u, r%u, r%u", addr, instr, rd, rs1, rs2);
            break;
        }
        
        case OP_EXIT: {
            snprintf(buffer, sizeof(buffer), "0x%06X: [%08X] exit", addr, instr);
            break;
        }
        
        default:
            snprintf(buffer, sizeof(buffer), "0x%06X: [%08X] ??? (unknown opcode 0x%X)", addr, instr, opcode);
            break;
    }
    
    char line[256];
    snprintf(line, sizeof(line), "          │ %s\n", buffer);
    write(STDOUT_FILENO, line, strlen(line));
}
