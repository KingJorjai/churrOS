/*
 * memory.h
 * Physical Memory and Memory Management Unit definitions
 */

#ifndef CHURROS_MEMORY_H
#define CHURROS_MEMORY_H

#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * MEMORY CONFIGURATION
 * ============================================ */

#define ADDR_BUS_BITS       24                          /* 24-bit address bus */
#define ADDR_SPACE_SIZE     (1 << ADDR_BUS_BITS)        /* 16MB address space */
#define WORD_SIZE           4                            /* 4 bytes per word */
#define PAGE_SIZE           4096                         /* 4KB pages */
#define PAGE_BITS           12                           /* log2(PAGE_SIZE) */
#define NUM_PAGES           (ADDR_SPACE_SIZE / PAGE_SIZE) /* 4096 pages */

#define KERNEL_RESERVED_PAGES  256                       /* Pages reserved for kernel (1MB) */
#define KERNEL_SIZE            (KERNEL_RESERVED_PAGES * PAGE_SIZE)

/* Virtual address breakdown (24 bits):
 * - Bits 23-12: Page number (12 bits = 4096 pages)
 * - Bits 11-0:  Offset within page (12 bits = 4096 bytes)
 */
#define VPN_MASK            0xFFF000  /* Virtual Page Number mask */
#define OFFSET_MASK         0x000FFF  /* Page offset mask */
#define GET_VPN(addr)       (((addr) & VPN_MASK) >> PAGE_BITS)
#define GET_OFFSET(addr)    ((addr) & OFFSET_MASK)
#define MAKE_ADDR(vpn, offset) (((vpn) << PAGE_BITS) | (offset))

/* Page Table Entry */
typedef struct {
    uint32_t valid    : 1;   /* Valid bit */
    uint32_t present  : 1;   /* Present in physical memory */
    uint32_t dirty    : 1;   /* Modified bit */
    uint32_t accessed : 1;   /* Accessed bit */
    uint32_t pfn      : 20;  /* Physical Frame Number (20 bits for 4096 frames) */
    uint32_t reserved : 8;   /* Reserved for future use */
} PageTableEntry;

/* Page Table */
typedef struct {
    PageTableEntry entries[NUM_PAGES];
    uint32_t physical_address;  /* Physical address where this page table resides */
} PageTable;

/* TLB Entry (Translation Lookaside Buffer) */
typedef struct {
    uint32_t valid;      /* Valid bit */
    uint32_t vpn;        /* Virtual Page Number */
    uint32_t pfn;        /* Physical Frame Number */
} TLBEntry;

#define TLB_SIZE 16  /* Number of TLB entries */

/* TLB */
typedef struct {
    TLBEntry entries[TLB_SIZE];
    uint32_t next_replace;  /* For round-robin replacement */
} TLB;

/* Memory Management Unit (one per HW thread) */
typedef struct {
    TLB tlb;
    uint32_t ptbr;          /* Page Table Base Register (physical address) */
    uint32_t pc;            /* Program Counter */
    uint32_t ir;            /* Instruction Register */
    int32_t registers[16];  /* 16 general purpose registers */
} MMU;

/* Physical Memory */
typedef struct {
    uint8_t* memory;                    /* Raw memory storage */
    uint32_t size;                      /* Total size in bytes */
    uint8_t* frame_bitmap;              /* Bitmap for free/allocated frames */
    uint32_t num_frames;                /* Total number of frames */
    uint32_t kernel_end_frame;          /* First frame after kernel space */
    pthread_mutex_t mutex;              /* Protection for concurrent access */
} PhysicalMemory;

/* ============================================
 * FUNCTION DECLARATIONS
 * ============================================ */

/* Physical Memory Management */
PhysicalMemory* physical_memory_create(void);
void physical_memory_destroy(PhysicalMemory* mem);
uint32_t physical_memory_allocate_frame(PhysicalMemory* mem);
void physical_memory_free_frame(PhysicalMemory* mem, uint32_t frame_num);
uint32_t physical_memory_read_word(PhysicalMemory* mem, uint32_t physical_addr);
void physical_memory_write_word(PhysicalMemory* mem, uint32_t physical_addr, uint32_t value);
void physical_memory_dump(PhysicalMemory* mem, uint32_t start_addr, uint32_t num_words);

/* MMU Management */
MMU* mmu_create(void);
void mmu_destroy(MMU* mmu);
void mmu_reset(MMU* mmu);
void mmu_set_ptbr(MMU* mmu, uint32_t ptbr);
uint32_t mmu_translate(MMU* mmu, PhysicalMemory* mem, uint32_t virtual_addr, int is_write);
void mmu_tlb_flush(MMU* mmu);

/* Page Table Management */
PageTable* page_table_create(PhysicalMemory* mem);
void page_table_destroy(PageTable* pt, PhysicalMemory* mem);
void page_table_map(PageTable* pt, PhysicalMemory* mem, uint32_t vpn, uint32_t pfn);
uint32_t page_table_lookup(PageTable* pt, PhysicalMemory* mem, uint32_t vpn);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_MEMORY_H */
