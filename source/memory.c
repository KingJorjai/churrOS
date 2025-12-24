/*
 * memory.c
 * Implementation of Physical Memory and MMU
 */

#include "../include/memory.h"
#include "../include/logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ============================================
 * PHYSICAL MEMORY IMPLEMENTATION
 * ============================================ */

PhysicalMemory* physical_memory_create(void)
{
    PhysicalMemory* mem = (PhysicalMemory*)malloc(sizeof(PhysicalMemory));
    if (!mem) return NULL;
    
    mem->size = ADDR_SPACE_SIZE;
    mem->num_frames = NUM_PAGES;
    mem->kernel_end_frame = KERNEL_RESERVED_PAGES;
    
    /* Allocate memory */
    mem->memory = (uint8_t*)calloc(ADDR_SPACE_SIZE, 1);
    if (!mem->memory) {
        free(mem);
        return NULL;
    }
    
    /* Allocate frame bitmap (1 bit per frame, rounded up to bytes) */
    uint32_t bitmap_size = (mem->num_frames + 7) / 8;
    mem->frame_bitmap = (uint8_t*)calloc(bitmap_size, 1);
    if (!mem->frame_bitmap) {
        free(mem->memory);
        free(mem);
        return NULL;
    }
    
    /* Mark kernel frames as allocated */
    for (uint32_t i = 0; i < KERNEL_RESERVED_PAGES; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        mem->frame_bitmap[byte_idx] |= (1 << bit_idx);
    }
    
    pthread_mutex_init(&mem->mutex, NULL);
    
    LOG_INFO(LOG_COMPONENT_MEMORY,
           "Created: %u MB total, %u KB kernel reserved",
           ADDR_SPACE_SIZE / (1024*1024), KERNEL_SIZE / 1024);
    LOG_INFO(LOG_COMPONENT_MEMORY,
           "%u pages total, %u kernel pages, %u user pages available",
           NUM_PAGES, KERNEL_RESERVED_PAGES, NUM_PAGES - KERNEL_RESERVED_PAGES);
    
    return mem;
}

void physical_memory_destroy(PhysicalMemory* mem)
{
    if (!mem) return;
    
    pthread_mutex_destroy(&mem->mutex);
    free(mem->frame_bitmap);
    free(mem->memory);
    free(mem);
}

uint32_t physical_memory_allocate_frame(PhysicalMemory* mem)
{
    if (!mem) return 0xFFFFFFFF;
    
    pthread_mutex_lock(&mem->mutex);
    
    /* Find first free frame after kernel space */
    for (uint32_t i = mem->kernel_end_frame; i < mem->num_frames; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(mem->frame_bitmap[byte_idx] & (1 << bit_idx))) {
            /* Frame is free, allocate it */
            mem->frame_bitmap[byte_idx] |= (1 << bit_idx);
            pthread_mutex_unlock(&mem->mutex);
            return i;
        }
    }
    
    pthread_mutex_unlock(&mem->mutex);
    LOG_ERROR(LOG_COMPONENT_MEMORY, "Out of physical memory!");
    return 0xFFFFFFFF;
}

void physical_memory_free_frame(PhysicalMemory* mem, uint32_t frame_num)
{
    if (!mem || frame_num >= mem->num_frames) return;
    if (frame_num < mem->kernel_end_frame) {
        LOG_WARN(LOG_COMPONENT_MEMORY, "Attempt to free kernel frame %u", frame_num);
        return;
    }
    
    pthread_mutex_lock(&mem->mutex);
    uint32_t byte_idx = frame_num / 8;
    uint32_t bit_idx = frame_num % 8;
    mem->frame_bitmap[byte_idx] &= ~(1 << bit_idx);
    pthread_mutex_unlock(&mem->mutex);
    
    LOG_DEBUG(LOG_COMPONENT_MEMORY, "Frame freed: PFN=%u (physical addr 0x%06X)", frame_num, frame_num * PAGE_SIZE);
}

uint32_t physical_memory_read_word(PhysicalMemory* mem, uint32_t physical_addr)
{
    if (!mem || physical_addr + WORD_SIZE > mem->size) {
        LOG_ERROR(LOG_COMPONENT_MEMORY, "Read out of bounds at 0x%06X", physical_addr);
        return 0;
    }
    
    /* Ensure word-aligned */
    physical_addr &= ~0x3;
    
    pthread_mutex_lock(&mem->mutex);
    uint32_t value = *(uint32_t*)(mem->memory + physical_addr);
    pthread_mutex_unlock(&mem->mutex);
    
    return value;
}

void physical_memory_write_word(PhysicalMemory* mem, uint32_t physical_addr, uint32_t value)
{
    if (!mem || physical_addr + WORD_SIZE > mem->size) {
        LOG_ERROR(LOG_COMPONENT_MEMORY, "Write out of bounds at 0x%06X", physical_addr);
        return;
    }
    
    /* Ensure word-aligned */
    physical_addr &= ~0x3;
    
    pthread_mutex_lock(&mem->mutex);
    *(uint32_t*)(mem->memory + physical_addr) = value;
    pthread_mutex_unlock(&mem->mutex);
}

void physical_memory_dump(PhysicalMemory* mem, uint32_t start_addr, uint32_t num_words)
{
    if (!mem) return;
    
    LOG_DEBUG(LOG_COMPONENT_MEMORY, "Memory dump 0x%06X - 0x%06X:", start_addr, start_addr + num_words * WORD_SIZE);
    
    /* Only dump details in DEBUG level */
    if (log_get_level() > LOG_LEVEL_DEBUG) return;
    
    char line[256];
    for (uint32_t i = 0; i < num_words; i++) {
        uint32_t addr = start_addr + i * WORD_SIZE;
        if (addr + WORD_SIZE > mem->size) break;
        
        uint32_t value = physical_memory_read_word(mem, addr);
        
        if (i % 4 == 0) {
            if (i > 0) {
                char outbuf[256];
                snprintf(outbuf, sizeof(outbuf), "          │ %s\n", line);
                write(STDOUT_FILENO, outbuf, strlen(outbuf));
            }
            snprintf(line, sizeof(line), "0x%06X: %08X", addr, value);
        } else {
            char temp[32];
            snprintf(temp, sizeof(temp), " %08X", value);
            strcat(line, temp);
        }
    }
    if (num_words > 0) {
        char outbuf[256];
        snprintf(outbuf, sizeof(outbuf), "          │ %s\n", line);
        write(STDOUT_FILENO, outbuf, strlen(outbuf));
    }
}

/* ============================================
 * MMU IMPLEMENTATION
 * ============================================ */

/* Helper: Pack PTE into uint32_t */
static inline uint32_t pte_pack(PageTableEntry* pte)
{
    return (pte->valid & 0x1) |
           ((pte->present & 0x1) << 1) |
           ((pte->dirty & 0x1) << 2) |
           ((pte->accessed & 0x1) << 3) |
           ((pte->pfn & 0xFFFFF) << 4) |
           ((pte->reserved & 0xFF) << 24);
}

/* Helper: Unpack uint32_t into PTE */
static inline void pte_unpack(PageTableEntry* pte, uint32_t data)
{
    pte->valid = data & 0x1;
    pte->present = (data >> 1) & 0x1;
    pte->dirty = (data >> 2) & 0x1;
    pte->accessed = (data >> 3) & 0x1;
    pte->pfn = (data >> 4) & 0xFFFFF;
    pte->reserved = (data >> 24) & 0xFF;
}

MMU* mmu_create(void)
{
    MMU* mmu = (MMU*)malloc(sizeof(MMU));
    if (!mmu) return NULL;
    
    mmu_reset(mmu);
    return mmu;
}

void mmu_destroy(MMU* mmu)
{
    free(mmu);
}

void mmu_reset(MMU* mmu)
{
    if (!mmu) return;
    
    /* Clear all registers */
    memset(mmu->registers, 0, sizeof(mmu->registers));
    mmu->pc = 0;
    mmu->ir = 0;
    mmu->ptbr = 0;
    
    /* Invalidate TLB */
    mmu_tlb_flush(mmu);
}

void mmu_set_ptbr(MMU* mmu, uint32_t ptbr)
{
    if (!mmu) return;
    mmu->ptbr = ptbr;
    mmu_tlb_flush(mmu);  /* Flush TLB when page table changes */
}

void mmu_tlb_flush(MMU* mmu)
{
    if (!mmu) return;
    
    LOG_DEBUG(LOG_COMPONENT_MMU, "TLB flush: invalidating all %d entries", TLB_SIZE);
    
    for (int i = 0; i < TLB_SIZE; i++) {
        mmu->tlb.entries[i].valid = 0;
        mmu->tlb.entries[i].vpn = 0;
        mmu->tlb.entries[i].pfn = 0;
    }
    mmu->tlb.next_replace = 0;
}

uint32_t mmu_translate(MMU* mmu, PhysicalMemory* mem, uint32_t virtual_addr, int is_write)
{
    if (!mmu || !mem) return 0xFFFFFFFF;
    
    uint32_t vpn = GET_VPN(virtual_addr);
    uint32_t offset = GET_OFFSET(virtual_addr);
    
    /* 1. Check TLB first */
    for (int i = 0; i < TLB_SIZE; i++) {
        if (mmu->tlb.entries[i].valid && mmu->tlb.entries[i].vpn == vpn) {
            /* TLB hit */
            uint32_t pfn = mmu->tlb.entries[i].pfn;
            uint32_t physical_addr = (pfn << PAGE_BITS) | offset;
            LOG_DEBUG(LOG_COMPONENT_MMU, "TLB HIT: VPN=%u -> PFN=%u (vaddr=0x%06X -> paddr=0x%06X) %s",
                     vpn, pfn, virtual_addr, physical_addr, is_write ? "[WRITE]" : "[READ]");
            return physical_addr;
        }
    }
    
    /* 2. TLB miss - walk page table */
    LOG_DEBUG(LOG_COMPONENT_MMU, "TLB MISS: VPN=%u (vaddr=0x%06X) - walking page table at PTBR=0x%06X",
             vpn, virtual_addr, mmu->ptbr);
    
    /* Page table is stored in physical memory at PTBR */
    uint32_t pte_addr = mmu->ptbr + vpn * sizeof(PageTableEntry);
    
    /* Read PTE from physical memory */
    uint32_t pte_data = physical_memory_read_word(mem, pte_addr);
    PageTableEntry pte;
    pte_unpack(&pte, pte_data);
    
    if (!pte.valid || !pte.present) {
        LOG_ERROR(LOG_COMPONENT_MMU, "PAGE FAULT: VPN=%u at vaddr=0x%06X (valid=%d, present=%d)",
                 vpn, virtual_addr, pte.valid, pte.present);
        return 0xFFFFFFFF;
    }
    
    /* 3. Update TLB (round-robin replacement) */
    uint32_t tlb_idx = mmu->tlb.next_replace;
    LOG_DEBUG(LOG_COMPONENT_MMU, "TLB UPDATE: Adding VPN=%u -> PFN=%u to TLB[%u]",
             vpn, pte.pfn, tlb_idx);
    mmu->tlb.entries[tlb_idx].valid = 1;
    mmu->tlb.entries[tlb_idx].vpn = vpn;
    mmu->tlb.entries[tlb_idx].pfn = pte.pfn;
    mmu->tlb.next_replace = (mmu->tlb.next_replace + 1) % TLB_SIZE;
    
    /* 4. Mark accessed (and dirty if write) */
    int bits_updated = 0;
    if (is_write && !pte.dirty) {
        pte.dirty = 1;
        bits_updated = 1;
        LOG_DEBUG(LOG_COMPONENT_MMU, "Page marked DIRTY: VPN=%u PFN=%u", vpn, pte.pfn);
    }
    if (!pte.accessed) {
        pte.accessed = 1;
        bits_updated = 1;
        LOG_DEBUG(LOG_COMPONENT_MMU, "Page marked ACCESSED: VPN=%u PFN=%u", vpn, pte.pfn);
    }
    
    if (bits_updated) {
        pte_data = pte_pack(&pte);
        physical_memory_write_word(mem, pte_addr, pte_data);
    }
    
    /* 5. Return physical address */
    uint32_t pfn = pte.pfn;
    uint32_t physical_addr = (pfn << PAGE_BITS) | offset;
    LOG_DEBUG(LOG_COMPONENT_MMU, "Translation complete: VPN=%u -> PFN=%u (vaddr=0x%06X -> paddr=0x%06X) %s",
             vpn, pfn, virtual_addr, physical_addr, is_write ? "[WRITE]" : "[READ]");
    
    return physical_addr;
}

/* ============================================
 * PAGE TABLE IMPLEMENTATION
 * ============================================ */

PageTable* page_table_create(PhysicalMemory* mem)
{
    if (!mem) return NULL;
    
    PageTable* pt = (PageTable*)malloc(sizeof(PageTable));
    if (!pt) return NULL;
    
    /* Initialize all entries as invalid */
    for (uint32_t i = 0; i < NUM_PAGES; i++) {
        pt->entries[i].valid = 0;
        pt->entries[i].present = 0;
        pt->entries[i].dirty = 0;
        pt->entries[i].accessed = 0;
        pt->entries[i].pfn = 0;
        pt->entries[i].reserved = 0;
    }
    
    /* Allocate physical frames for the page table in kernel space
     * Page table size = NUM_PAGES * sizeof(PageTableEntry) = 4096 * 4 = 16KB
     * This requires 4 pages (16KB / 4KB)
     */
    uint32_t pt_size = NUM_PAGES * sizeof(PageTableEntry);
    uint32_t pt_pages_needed = (pt_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    static uint32_t next_kernel_page = 0;  /* Keeps track of kernel space allocation */
    
    if (next_kernel_page + pt_pages_needed > KERNEL_RESERVED_PAGES) {
        LOG_CRITICAL(LOG_COMPONENT_MEMORY, "Kernel space exhausted!");
        free(pt);
        return NULL;
    }
    
    pt->physical_address = next_kernel_page * PAGE_SIZE;
    next_kernel_page += pt_pages_needed;
    
    /* Write page table to physical memory */
    for (uint32_t i = 0; i < NUM_PAGES; i++) {
        uint32_t pte_addr = pt->physical_address + i * sizeof(PageTableEntry);
        uint32_t pte_data = pte_pack(&pt->entries[i]);
        physical_memory_write_word(mem, pte_addr, pte_data);
    }
    
    return pt;
}

void page_table_destroy(PageTable* pt, PhysicalMemory* mem)
{
    if (!pt) return;
    
    /* Free all allocated frames */
    for (uint32_t i = 0; i < NUM_PAGES; i++) {
        if (pt->entries[i].valid && pt->entries[i].present) {
            physical_memory_free_frame(mem, pt->entries[i].pfn);
        }
    }
    
    free(pt);
}

void page_table_map(PageTable* pt, PhysicalMemory* mem, uint32_t vpn, uint32_t pfn)
{
    if (!pt || !mem || vpn >= NUM_PAGES) return;
    
    pt->entries[vpn].valid = 1;
    pt->entries[vpn].present = 1;
    pt->entries[vpn].dirty = 0;
    pt->entries[vpn].accessed = 0;
    pt->entries[vpn].pfn = pfn;
    
    LOG_DEBUG(LOG_COMPONENT_MEMORY, "Page mapped: VPN=%u -> PFN=%u (vaddr=0x%06X -> paddr=0x%06X)",
             vpn, pfn, vpn << PAGE_BITS, pfn << PAGE_BITS);
    
    /* Update in physical memory */
    uint32_t pte_addr = pt->physical_address + vpn * sizeof(PageTableEntry);
    uint32_t pte_data = pte_pack(&pt->entries[vpn]);
    physical_memory_write_word(mem, pte_addr, pte_data);
}

uint32_t page_table_lookup(PageTable* pt, PhysicalMemory* mem, uint32_t vpn)
{
    if (!pt || !mem || vpn >= NUM_PAGES) return 0xFFFFFFFF;
    
    if (pt->entries[vpn].valid && pt->entries[vpn].present) {
        return pt->entries[vpn].pfn;
    }
    
    return 0xFFFFFFFF;
}
