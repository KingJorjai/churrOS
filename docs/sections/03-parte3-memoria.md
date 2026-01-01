# Parte 3: Gestión de Memoria Virtual

## Introducción

La tercera parte cierra el círculo: memoria virtual completa. Sobre lo que ya teníamos (Parte 1 y 2), añadimos `memory.c/h`, `loader.c/h` e `instruction.c/h`, más un generador de programas ELF que llamamos `prometheus`.

Qué implementamos:

- **MMU** (Memory Management Unit) con traducción virtual→física
- **TLB** (Translation Lookaside Buffer) de 8 entradas para cachear traducciones
- **Paginación** de 4KB con tabla de páginas por proceso
- **Loader** de programas ELF con asignación dinámica de frames
- **ISA** básico con instrucciones LOAD, STORE, ADD, EXIT

## Arquitectura de Memoria

Tenemos 16MB de direcciones (24 bits), divididos así:

- **Kernel Space**: 1MB (primeros 256 frames) reservados
- **User Space**: 15MB (3840 frames) para procesos de usuario

Cada página/frame mide 4KB (12 bits de offset).

```{.mermaid format=pdf}
graph TD
    VADDR[Dirección Virtual 24 bits] --> VPN[VPN: Virtual Page Number bits 23-12]
    VADDR --> OFFSET[Offset: bits 11-0]
    
    VPN --> TLB{TLB Lookup}
    TLB -->|HIT| PFN1[PFN from TLB]
    TLB -->|MISS| PT[Page Table Lookup]
    PT --> PFN2[PFN from Page Table]
    PFN2 --> UPDATE[Update TLB]
    UPDATE --> PFN1
    
    PFN1 --> PADDR["Dirección Física = (PFN shl 12) or Offset"]
    OFFSET --> PADDR
```

### Formato de Direcciones

```plaintext
Dirección Virtual (24 bits):
┌──────────────┬──────────────┐
│  VPN (12b)   │  Offset (12b)│
└──────────────┴──────────────┘
     bits 23-12      bits 11-0

Dirección Física (24 bits):
┌──────────────┬──────────────┐
│  PFN (12b)   │  Offset (12b)│
└──────────────┴──────────────┘
     bits 23-12      bits 11-0
```

- **VPN** (Virtual Page Number): Índice de página virtual (0-4095)
- **PFN** (Physical Frame Number): Índice de frame físico (0-4095)
- **Offset**: Desplazamiento dentro de página/frame (0-4095 bytes)

## Memoria Física

El componente `PhysicalMemory` gestiona los frames físicos mediante un bitmap de asignación.

### Estructura

```c
typedef struct {
    uint8_t* memory;              // 16 MB de RAM simulada
    uint8_t* frame_bitmap;        // Bitmap de frames (1 bit por frame)
    uint32_t size;                // Tamaño total (16 MB)
    uint32_t num_frames;          // Número total de frames (4096)
    uint32_t kernel_end_frame;    // Primer frame de usuario (256)
    
    uint32_t allocations;         // Total de frames asignados
    uint32_t frees;               // Total de frames liberados
    uint32_t peak_usage;          // Pico de frames de usuario usados
    
    pthread_mutex_t mutex;
} PhysicalMemory;
```

### Interfaz

```c
PhysicalMemory* physical_memory_create(void);
void physical_memory_destroy(PhysicalMemory* mem);

uint32_t physical_memory_allocate_frame(PhysicalMemory* mem);
void physical_memory_free_frame(PhysicalMemory* mem,
                                uint32_t frame_num);

uint32_t physical_memory_read_word(PhysicalMemory* mem,
                                   uint32_t physical_addr);
void physical_memory_write_word(PhysicalMemory* mem,
                                uint32_t physical_addr,
                                uint32_t value);
```

### Asignación de Frames

La asignación busca el primer frame libre después del espacio del kernel:

```c
uint32_t physical_memory_allocate_frame(PhysicalMemory* mem) {
    pthread_mutex_lock(&mem->mutex);
    
    for (uint32_t i = mem->kernel_end_frame; i < mem->num_frames; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(mem->frame_bitmap[byte_idx] & (1 << bit_idx))) {
            // Frame libre, asignarlo
            mem->frame_bitmap[byte_idx] |= (1 << bit_idx);
            mem->allocations++;
            
            // Actualizar pico de uso
            uint32_t current_usage = count_allocated_user_frames(mem);
            if (current_usage > mem->peak_usage) {
                mem->peak_usage = current_usage;
            }
            
            pthread_mutex_unlock(&mem->mutex);
            return i;
        }
    }
    
    pthread_mutex_unlock(&mem->mutex);
    LOG_ERROR(LOG_COMPONENT_MEMORY,
              "Out of physical memory!");
    return 0xFFFFFFFF;
}
```

## TLB (Translation Lookaside Buffer)

El TLB es una caché asociativa de 8 entradas que guarda traducciones VPN→PFN recientes. Reduce un montón los accesos a la Page Table —en nuestras pruebas vimos mejoras de 6-10x.

### Estructura

```c
typedef struct {
    uint32_t vpn;        // Virtual Page Number
    uint32_t pfn;        // Physical Frame Number
    int valid;           // Entrada válida
    uint64_t last_used;  // Timestamp para LRU
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_SIZE];  // 8 entradas
    uint32_t hits;               // TLB hits
    uint32_t misses;             // TLB misses
    uint64_t access_counter;     // Contador para LRU
    pthread_mutex_t mutex;
} TLB;
```

### Interfaz

```c
TLB* tlb_create(void);
void tlb_destroy(TLB* tlb);

int tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t* out_pfn);
void tlb_update(TLB* tlb, uint32_t vpn, uint32_t pfn);
void tlb_invalidate(TLB* tlb);
void tlb_get_stats(TLB* tlb, uint32_t* out_hits,
                   uint32_t* out_misses);
```

### Búsqueda

```c
int tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t* out_pfn) {
    pthread_mutex_lock(&tlb->mutex);
    
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb->entries[i].valid &&
            tlb->entries[i].vpn == vpn) {
            *out_pfn = tlb->entries[i].pfn;
            tlb->entries[i].last_used = ++tlb->access_counter;
            tlb->hits++;
            
            pthread_mutex_unlock(&tlb->mutex);
            
            LOG_TRACE(LOG_COMPONENT_MMU,
                     "TLB HIT: VPN=0x%03X -> PFN=0x%03X",
                     vpn, *out_pfn);
            return 1;  // HIT
        }
    }
    
    tlb->misses++;
    pthread_mutex_unlock(&tlb->mutex);
    
    LOG_TRACE(LOG_COMPONENT_MMU, "TLB MISS: VPN=0x%03X", vpn);
    return 0;  // MISS
}
```

### Reemplazo LRU

Cuando se actualiza el TLB en un MISS, se usa LRU (Least Recently Used) para elegir la entrada víctima:

```c
void tlb_update(TLB* tlb, uint32_t vpn, uint32_t pfn) {
    pthread_mutex_lock(&tlb->mutex);
    
    // Buscar entrada inválida primero
    for (int i = 0; i < TLB_SIZE; i++) {
        if (!tlb->entries[i].valid) {
            tlb->entries[i].vpn = vpn;
            tlb->entries[i].pfn = pfn;
            tlb->entries[i].valid = 1;
            tlb->entries[i].last_used = ++tlb->access_counter;
            
            pthread_mutex_unlock(&tlb->mutex);
            LOG_TRACE(LOG_COMPONENT_MMU,
                     "TLB UPDATE: VPN=0x%03X -> PFN=0x%03X "
                     "(free slot)",
                     vpn, pfn);
            return;
        }
    }
    
    // Encontrar entrada LRU
    int lru_idx = 0;
    uint64_t min_last_used = tlb->entries[0].last_used;
    for (int i = 1; i < TLB_SIZE; i++) {
        if (tlb->entries[i].last_used < min_last_used) {
            min_last_used = tlb->entries[i].last_used;
            lru_idx = i;
        }
    }
    
    // Reemplazar entrada LRU
    tlb->entries[lru_idx].vpn = vpn;
    tlb->entries[lru_idx].pfn = pfn;
    tlb->entries[lru_idx].valid = 1;
    tlb->entries[lru_idx].last_used = ++tlb->access_counter;
    
    pthread_mutex_unlock(&tlb->mutex);
    LOG_TRACE(LOG_COMPONENT_MMU,
             "TLB UPDATE: VPN=0x%03X -> PFN=0x%03X "
             "(LRU eviction)",
             vpn, pfn);
}
```

## Page Table

Cada proceso tiene su propia tabla de páginas que mapea VPN→PFN.

### Estructura

```c
typedef struct {
    uint32_t pfn;      // Physical Frame Number
    int valid;         // Entrada válida (página asignada)
    int present;       // Página presente en RAM (no swapped)
} PageTableEntry;

typedef struct {
    PageTableEntry entries[NUM_PAGES];  // 4096 entradas (1 por página virtual)
    uint32_t num_allocated_pages;       // Contador de páginas asignadas
} PageTable;
```

### Interfaz

```c
PageTable* page_table_create(void);
void page_table_destroy(PageTable* pt, PhysicalMemory* mem);

int page_table_map(PageTable* pt, uint32_t vpn,
                   PhysicalMemory* mem);
uint32_t page_table_lookup(PageTable* pt, uint32_t vpn);
void page_table_unmap(PageTable* pt, uint32_t vpn,
                      PhysicalMemory* mem);
```

### Asignación bajo Demanda

No asignamos todas las páginas al cargar el programa —¿para qué? Usamos demand paging: cuando se accede a una página no mapeada, `page_table_map()` pide un frame físico y crea la entrada ahí mismo.

```c
int page_table_map(PageTable* pt, uint32_t vpn,
                   PhysicalMemory* mem) {
    if (vpn >= NUM_PAGES) return -1;
    
    if (pt->entries[vpn].valid) {
        return 0;  // Ya mapeada
    }
    
    // Solicitar frame físico
    uint32_t pfn = physical_memory_allocate_frame(mem);
    if (pfn == 0xFFFFFFFF) {
        LOG_ERROR(LOG_COMPONENT_MEMORY,
                 "Failed to allocate frame for VPN=0x%03X",
                 vpn);
        return -1;
    }
    
    // Crear entrada
    pt->entries[vpn].pfn = pfn;
    pt->entries[vpn].valid = 1;
    pt->entries[vpn].present = 1;
    pt->num_allocated_pages++;
    
    LOG_DEBUG(LOG_COMPONENT_MEMORY,
             "Mapped VPN=0x%03X -> PFN=0x%03X", vpn, pfn);
    
    return 0;
}
```

## MMU (Memory Management Unit)

La MMU combina TLB y Page Table para realizar la traducción de direcciones virtuales a físicas.

### Estructura

```c
typedef struct {
    TLB* tlb;
    PageTable* page_table;
    PhysicalMemory* physical_memory;
    pthread_mutex_t mutex;
} MMU;
```

### Traducción de Direcciones

```c
uint32_t mmu_translate(MMU* mmu, uint32_t virtual_addr) {
    uint32_t vpn = virtual_addr >> PAGE_SHIFT;     // bits 23-12
    uint32_t offset = virtual_addr & PAGE_MASK;    // bits 11-0
    uint32_t pfn;
    
    // 1. Intentar TLB lookup
    if (tlb_lookup(mmu->tlb, vpn, &pfn)) {
        // TLB HIT
        uint32_t physical_addr = (pfn << PAGE_SHIFT) | offset;
        LOG_TRACE(LOG_COMPONENT_MMU,
                 "Translate 0x%06X -> 0x%06X (TLB HIT)",
                 virtual_addr, physical_addr);
        return physical_addr;
    }
    
    // 2. TLB MISS: consultar Page Table
    pfn = page_table_lookup(mmu->page_table, vpn);
    if (pfn == 0xFFFFFFFF) {
        // Página no mapeada: asignar bajo demanda
        if (page_table_map(mmu->page_table, vpn,
                          mmu->physical_memory) != 0) {
            LOG_ERROR(LOG_COMPONENT_MMU, "Page fault at VPN=0x%03X", vpn);
            return 0xFFFFFFFF;
        }
        pfn = page_table_lookup(mmu->page_table, vpn);
    }
    
    // 3. Actualizar TLB con la traducción
    tlb_update(mmu->tlb, vpn, pfn);
    
    uint32_t physical_addr = (pfn << PAGE_SHIFT) | offset;
    LOG_TRACE(LOG_COMPONENT_MMU,
             "Translate 0x%06X -> 0x%06X (TLB MISS, PT HIT)",
             virtual_addr, physical_addr);
    return physical_addr;
}
```

### Lectura/Escritura

```c
uint32_t mmu_read_word(MMU* mmu, uint32_t virtual_addr) {
    uint32_t physical_addr = mmu_translate(mmu, virtual_addr);
    if (physical_addr == 0xFFFFFFFF) return 0;
    
    return physical_memory_read_word(mmu->physical_memory,
                                     physical_addr);
}

void mmu_write_word(MMU* mmu, uint32_t virtual_addr,
                    uint32_t value) {
    uint32_t physical_addr = mmu_translate(mmu, virtual_addr);
    if (physical_addr == 0xFFFFFFFF) return;
    
    physical_memory_write_word(mmu->physical_memory,
                              physical_addr, value);
}
```

## Conjunto de Instrucciones (ISA)

Para validar que la memoria virtual funciona, implementamos un ISA minimalista con 4 instrucciones. No hace falta más:

### Formato de Instrucción

```c
typedef struct {
    uint8_t opcode;         // Código de operación
    uint32_t operand;       // Operando (dirección o valor)
} Instruction;
```

### Instrucciones

| Opcode | Mnemónico | Descripción |
|--------|-----------|-------------|
| 0x01   | `LOAD addr` | Cargar palabra de `addr` virtual en registro |
| 0x02   | `STORE addr` | Almacenar registro en `addr` virtual |
| 0x03   | `ADD value` | Sumar `value` al registro |
| 0xFF   | `EXIT` | Terminar proceso |

### Ejecución

```c
int instruction_execute(Instruction* inst, MMU* mmu,
                        uint32_t* reg) {
    switch (inst->opcode) {
        case OPCODE_LOAD:
            *reg = mmu_read_word(mmu, inst->operand);
            LOG_DEBUG(LOG_COMPONENT_ISA,
                     "LOAD [0x%06X] -> R = %u",
                     inst->operand, *reg);
            return 0;
            
        case OPCODE_STORE:
            mmu_write_word(mmu, inst->operand, *reg);
            LOG_DEBUG(LOG_COMPONENT_ISA,
                     "STORE R(%u) -> [0x%06X]",
                     *reg, inst->operand);
            return 0;
            
        case OPCODE_ADD:
            *reg += inst->operand;
            LOG_DEBUG(LOG_COMPONENT_ISA,
                     "ADD R += %u -> R = %u",
                     inst->operand, *reg);
            return 0;
            
        case OPCODE_EXIT:
            LOG_INFO(LOG_COMPONENT_ISA, "EXIT");
            return 1;  // Terminar proceso
            
        default:
            LOG_ERROR(LOG_COMPONENT_ISA,
                     "Invalid opcode 0x%02X", inst->opcode);
            return -1;
    }
}
```

## Loader de Programas ELF

El Loader carga programas en formato ELF simplificado y crea procesos con memoria virtual inicializada.

### Formato ELF Simplificado

```c
typedef struct {
    uint32_t magic;           // 0x464C4500 ("ELF\0")
    uint32_t code_size;       // Tamaño del segmento (bytes)
    uint32_t entry_point;     // Dirección virtual de inicio
    uint32_t num_instructions; // Número de instrucciones
} ELFHeader;
```

El archivo ELF contiene:
1. Header (16 bytes)
2. Segmento de código (instrucciones consecutivas)

### Carga de Programas

```c
PCB* loader_load_program(Loader* loader, const char* elf_path) {
    // 1. Leer archivo ELF
    FILE* f = fopen(elf_path, "rb");
    if (!f) return NULL;
    
    ELFHeader header;
    fread(&header, sizeof(ELFHeader), 1, f);
    
    if (header.magic != ELF_MAGIC) {
        fclose(f);
        return NULL;
    }
    
    // 2. Crear proceso
    uint32_t pid = kernel_allocate_pid(loader->kernel);
    PCB* pcb = pcb_create(pid, 0);  // TTL=0 (se calcula según instrucciones)
    
    // 3. Crear MMU para el proceso
    pcb->mmu = mmu_create(loader->kernel->physical_memory);
    
    // 4. Cargar instrucciones en memoria virtual
    Instruction inst;
    for (uint32_t i = 0; i < header.num_instructions; i++) {
        fread(&inst, sizeof(Instruction), 1, f);
        
        uint32_t virtual_addr = header.entry_point + (i * sizeof(Instruction));
        
        // Escribir instrucción en memoria virtual
        mmu_write_word(pcb->mmu, virtual_addr, *((uint32_t*)&inst));
    }
    
    pcb->pc = header.entry_point;  // Program Counter inicial
    pcb->state = PROCESS_STATE_READY;
    
    fclose(f);
    
    LOG_INFO(LOG_COMPONENT_LOADER,
            "Programa %s creado: PID=%u, %u instrucciones",
            elf_path, pid, header.num_instructions);
    
    return pcb;
}
```

### Generador de Programas (Prometheus)

Para las pruebas creamos `prometheus`, un generador que escupe programas ELF aleatorios con secuencias de LOAD/STORE/ADD/EXIT. Las direcciones virtuales están distribuidas para forzar el uso de múltiples páginas y probar bien el TLB.

```bash
$ ./prometheus/heracles -n 10 -s 100
Generated 10 ELF programs in elfs/ directory
```

Cada programa tiene entre 5 y 50 instrucciones, con direcciones virtuales que fuerzan el uso de múltiples páginas (validando paginación y TLB).

## Integración con el Kernel

Cada hardware thread ahora ejecuta instrucciones del proceso asignado en cada tick:

```c
void machine_advance_cycle(Kernel* kernel) {
    for (each hardware thread) {
        PCB* current = thread->current_pcb;
        
        if (current && current->pid != 0) {
            // Leer instrucción desde PC
            uint32_t inst_word =
                mmu_read_word(current->mmu, current->pc);
            Instruction inst = *((Instruction*)&inst_word);
            
            // Ejecutar instrucción
            int result = instruction_execute(&inst, current->mmu,
                                            &current->reg);
            
            if (result == 1) {
                // EXIT ejecutado
                current->ttl = 0;
                kernel_signal_scheduler(kernel);
            } else if (result == 0) {
                // Instrucción normal
                current->pc += sizeof(Instruction);
                current->ttl--;
                current->ticks_since_swap++;
                current->temperature++;
                
                if (current->ttl == 0 ||
                    quantum_expired(current, kernel->config)) {
                    kernel_signal_scheduler(kernel);
                }
            }
        }
    }
}
```

## Resultados de Pruebas

### TLB Hit Rate

Las pruebas muestran tasas de acierto del TLB entre 85% y 95%, validando la localidad espacial y temporal de los accesos a memoria.

```plaintext
CPU 0 Core 0 Thread 0:
  TLB: 68 hits, 11 misses (86.1% hit rate)
CPU 0 Core 0 Thread 1:
  TLB: 142 hits, 18 misses (88.8% hit rate)
```

### Uso de Memoria

Las estadísticas confirman que la asignación bajo demanda funciona correctamente:

```plaintext
Memory: 9/3840 user frames (0.2%), peak: 9 (0.2%)
  Allocations: 9, Frees: 0
```

Cada proceso usa aproximadamente 1-2 frames (dependiendo de cuántas páginas virtuales accede).

### Trazas de Ejecución

```plaintext
[MMU] TLB MISS: VPN=0x000
[MEM] Mapped VPN=0x000 -> PFN=0x100
[MMU] TLB UPDATE: VPN=0x000 -> PFN=0x100
[MMU] Translate 0x000000 -> 0x100000 (TLB MISS, PT HIT)
[ISA] LOAD [0x000000] -> R = 42
[MMU] TLB HIT: VPN=0x000 -> PFN=0x100
[MMU] Translate 0x000004 -> 0x100004 (TLB HIT)
[ISA] ADD R += 10 -> R = 52
[MMU] TLB HIT: VPN=0x000 -> PFN=0x100
[MMU] Translate 0x000008 -> 0x100008 (TLB HIT)
[ISA] STORE R(52) -> [0x000008]
```

Las trazas confirman que:
1. Primer acceso a VPN genera TLB MISS y asignación de frame
2. Accesos subsiguientes a la misma página generan TLB HIT
3. Instrucciones se ejecutan correctamente
4. Traducciones virtuales→físicas son consistentes

## Decisiones de Diseño

### Tamaño de Página 4KB

Se ha elegido 4KB como tamaño de página, el estándar en arquitecturas x86/x64. Esta decisión equilibra fragmentación interna (páginas grandes desperdician memoria) y overhead de Page Table (páginas pequeñas requieren más entradas).

### TLB de 8 Entradas

Un TLB de 8 entradas es pequeño comparado con hardware real (~512 entradas en CPUs modernos), pero suficiente para este simulador. Permite observar tanto HITs como MISSes en las trazas, validando la política de reemplazo LRU.

### Demand Paging

Las páginas se asignan bajo demanda (no todas al cargar el programa). Esta decisión refleja el comportamiento de sistemas operativos reales y optimiza el uso de memoria (solo se asignan las páginas realmente accedidas).

### ISA Minimalista

El conjunto de instrucciones se ha limitado a 4 opcodes para mantener la simplicidad. Aunque limitado, es suficiente para validar todos los aspectos de la memoria virtual: lectura, escritura, traducción de direcciones, TLB, paginación y terminación de procesos.

### MMU por Proceso

Cada proceso tiene su propia MMU (con TLB y Page Table independientes). Esta decisión simplifica la implementación (no hay que invalidar TLBs en context switches) y refleja arquitecturas con TLBs tageados por ASID (Address Space Identifier).

\newpage
