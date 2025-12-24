/*
 * instruction.h
 * Instruction set and execution engine
 */

#ifndef CHURROS_INSTRUCTION_H
#define CHURROS_INSTRUCTION_H

#include <stdint.h>
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Instruction formats (all 32 bits) */
#define OPCODE_MASK     0xF0000000
#define OPCODE_SHIFT    28

/* Opcodes */
#define OP_LD           0x0   /* Load:  ld  Rd, addr */
#define OP_ST           0x1   /* Store: st  Rs, addr */
#define OP_ADD          0x2   /* Add:   add Rd, Rs1, Rs2 */
#define OP_EXIT         0xF   /* Exit:  exit */

/* Instruction format helpers */
#define GET_OPCODE(instr)    (((instr) & OPCODE_MASK) >> OPCODE_SHIFT)
#define GET_RD(instr)        (((instr) >> 24) & 0x0F)
#define GET_RS1(instr)       (((instr) >> 20) & 0x0F)
#define GET_RS2(instr)       (((instr) >> 16) & 0x0F)
#define GET_ADDR(instr)      ((instr) & 0x00FFFFFF)

/* Instruction type */
typedef uint32_t Instruction;

/* Execute one instruction
 * Returns: 1 if should continue, 0 if EXIT instruction */
int instruction_execute(Instruction instr, MMU* mmu, PhysicalMemory* mem);

/* Decode and print instruction for debugging */
void instruction_print(Instruction instr, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_INSTRUCTION_H */
