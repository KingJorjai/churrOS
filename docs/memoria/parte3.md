# Parte 3: Gestión de Memoria Virtual

## Introducción

La tercera parte del proyecto implementa un sistema completo de gestión de memoria virtual con paginación. El sistema simula:

- **Memoria física** de 16MB con páginas de 4KB
- **Memory Management Unit (MMU)** con TLB de 16 entradas por HW Thread
- **Tablas de páginas** residentes en espacio del kernel
- **Loader** que carga programas desde archivos `.elf`
- **Instruction set** (LD, ST, ADD, EXIT) con traducción virtual→física
- **Prometheus**: Generador de programas de prueba

## Arquitectura de Memoria

El sistema utiliza un bus de direcciones de 24 bits (2^24 = 16MB total) dividido en dos regiones principales: Kernel Space (0x000000-0x0FFFFF, 1MB) que aloja tablas de páginas y estructuras del kernel, y User Space (0x100000-0xFFFFFF, 15MB) que contiene código, datos y stack de procesos.

```{.mermaid format=pdf}
graph TD
    AS["Address Space: 16MB (2^24)"] --> KS["Kernel Space: 1MB"]
    AS --> US["User Space: 15MB"]
    KS --> PT["Page Tables"]
    KS --> KD["Kernel Data Structures"]
    US --> TC[".text - Code"]
    US --> TD[".data - Data"]
    US --> ST["Stack"]
```

### Configuración de Paginación

El sistema utiliza bus de 24 bits (16MB total), palabras de 4 bytes, páginas de 4KB (12 bits de offset), totalizando 4096 páginas con 256 reservadas para kernel (1MB).

### Descomposición de Dirección Virtual

Cada dirección virtual de 24 bits se divide en VPN (Virtual Page Number, bits 23-12) y Offset (bits 11-0), permitiendo direccionar 4096 páginas de 4096 bytes cada una.

## Estructuras de Datos

### Page Table Entry

Cada entrada de tabla de páginas (32 bits) contiene flags de valid (página asignada), present (en memoria física), dirty (modificada), accessed (usada recientemente) y el Physical Frame Number (PFN) de 20 bits que permite direccionar hasta 4GB de RAM.

### Page Table
} PageTable;
```

Cada proceso tiene su propia tabla de páginas. El tamaño de una tabla es:
```
4096 entradas × 4 bytes/entrada = 16KB por tabla
```

### Translation Lookaside Buffer (TLB)

El TLB es una cache de traducciones virtuales→físicas:

```c
typedef struct {
    uint32_t valid;  // Entrada válida
    uint32_t vpn;    // Virtual Page Number
    uint32_t pfn;    // Physical Frame Number
} TLBEntry;

#define TLB_SIZE 16  // 16 entradas por TLB

typedef struct {
    TLBEntry entries[TLB_SIZE];
    uint32_t next_replace;  // Para reemplazo round-robin
    
    // Estadísticas
    uint64_t hits;
    uint64_t misses;
} TLB;
```

**Política de reemplazo**: Round-robin (simple y determinista)

### Memory Management Unit (MMU)

Cada HW Thread tiene su propia MMU:

```c
typedef struct {
    TLB tlb;               // Translation Lookaside Buffer
    uint32_t ptbr;         // Page Table Base Register
    uint32_t pc;           // Program Counter
    uint32_t ir;           // Instruction Register
    int32_t registers[16]; // 16 registros de propósito general
} MMU;
```

**Registros especiales:**
- `ptbr`: Apunta a la tabla de páginas del proceso actual
- `pc`: Program Counter (dirección de siguiente instrucción)
- `ir`: Instruction Register (instrucción actual)

### Physical Memory

```c
typedef struct {
    uint8_t* data;                  // 16MB de memoria
    int free_frames[NUM_PAGES];     // Bitmap de frames libres
    pthread_mutex_t mutex;          // Sincronización
} PhysicalMemory;
```

El bitmap `free_frames` implementa un allocator simple:
- `1`: Frame libre
- `0`: Frame ocupado

## Proceso de Traducción de Direcciones

### Algoritmo Completo

```{.mermaid format=pdf}
flowchart TD
    A["1. CPU genera dirección virtual VA"] --> B["2. Extraer VPN = VA[23:12]"]
    B --> C["3. Buscar VPN en TLB"]
    C -->|Hit| G["7. Calcular dirección física:<br/>PA = (PFN << 12) | Offset"]
    C -->|Miss| D["4. Leer PTBR (base de Page Table)"]
    D --> E["5. Acceder PageTable[VPN]"]
    E -->|valid==0| F1["Page Fault"]
    E -->|valid==1| F2["6. Obtener PFN, actualizar TLB"]
    F2 --> G
    G --> H["8. Acceder memoria física en PA"]
```

### Implementación

```c
uint32_t mmu_translate(MMU* mmu, PhysicalMemory* pm, 
                       uint32_t virtual_addr, int is_write)
{
    uint32_t vpn = GET_VPN(virtual_addr);
    uint32_t offset = GET_OFFSET(virtual_addr);
    
    // 1. Buscar en TLB
    for (uint32_t i = 0; i < TLB_SIZE; i++) {
        if (mmu->tlb.entries[i].valid && 
            mmu->tlb.entries[i].vpn == vpn) {
            // TLB Hit
            mmu->tlb.hits++;
            uint32_t pfn = mmu->tlb.entries[i].pfn;
            return (pfn << PAGE_BITS) | offset;
        }
    }
    
    // 2. TLB Miss - Page Walk
    mmu->tlb.misses++;
    
    // 3. Obtener Page Table del proceso
    PageTable* pt = (PageTable*)(pm->data + mmu->ptbr);
    PageTableEntry* pte = &pt->entries[vpn];
    
    // 4. Verificar validez
    if (!pte->valid) {
        LOG_ERROR(LOG_COMPONENT_MEMORY, "Page fault! VPN=0x%X", vpn);
        return UINT32_MAX;  // Page fault
    }
    
    // 5. Actualizar bits de acceso
    pte->accessed = 1;
    if (is_write) {
        pte->dirty = 1;
    }
    
    // 6. Obtener PFN
    uint32_t pfn = pte->pfn;
    
    // 7. Actualizar TLB (round-robin replacement)
    uint32_t tlb_idx = mmu->tlb.next_replace;
    mmu->tlb.entries[tlb_idx].valid = 1;
    mmu->tlb.entries[tlb_idx].vpn = vpn;
    mmu->tlb.entries[tlb_idx].pfn = pfn;
    mmu->tlb.next_replace = (tlb_idx + 1) % TLB_SIZE;
    
    // 8. Calcular dirección física
    return (pfn << PAGE_BITS) | offset;
}
```

### Estadísticas de TLB

El sistema recolecta métricas de rendimiento:

```c
// Hit rate = hits / (hits + misses)
double tlb_hit_rate = (double)mmu->tlb.hits / 
                     (mmu->tlb.hits + mmu->tlb.misses);
```

Típicamente se observan hit rates de 80-95% dependiendo del patrón de acceso.

## Memory Layout del Proceso

### Estructura

Cada proceso tiene un layout de memoria definido:

```c
typedef struct {
    uint32_t pgb;          // Page Table Base (dirección física)
    uint32_t code_start;   // Inicio de .text
    uint32_t code_size;    // Tamaño de .text
    uint32_t data_start;   // Inicio de .data
    uint32_t data_size;    // Tamaño de .data
    uint32_t stack_start;  // Inicio de stack
    uint32_t stack_size;   // Tamaño de stack
} MemoryLayout;
```

**Ejemplo de layout:**
```
Virtual Address Space del Proceso:
0x000000 - 0x00FFFF: .text (code)     - 64KB
0x010000 - 0x01FFFF: .data (data)     - 64KB
0x020000 - 0x02FFFF: .stack (stack)   - 64KB
```

### Asignación de Memoria

El loader asigna páginas físicas para cada sección:

```c
int memory_allocate_pages(PhysicalMemory* pm, MemoryLayout* layout,
                         uint32_t code_pages, uint32_t data_pages)
{
    PageTable* pt = (PageTable*)(pm->data + layout->pgb);
    
    // Asignar páginas para .text
    for (uint32_t i = 0; i < code_pages; i++) {
        int frame = physical_memory_allocate_frame(pm);
        if (frame < 0) return -1;
        
        uint32_t vpn = GET_VPN(layout->code_start) + i;
        pt->entries[vpn].valid = 1;
        pt->entries[vpn].present = 1;
        pt->entries[vpn].pfn = frame;
    }
    
    // Asignar páginas para .data
    for (uint32_t i = 0; i < data_pages; i++) {
        int frame = physical_memory_allocate_frame(pm);
        if (frame < 0) return -1;
        
        uint32_t vpn = GET_VPN(layout->data_start) + i;
        pt->entries[vpn].valid = 1;
        pt->entries[vpn].present = 1;
        pt->entries[vpn].pfn = frame;
    }
    
    return 0;
}
```

## Instruction Set

### Formato de Instrucciones

Cada instrucción ocupa 4 bytes (una palabra):

```
┌────────┬────────┬────────┬────────┐
│ Opcode │   Rd   │   Rs   │   Imm  │
│ (8b)   │  (8b)  │  (8b)  │  (8b)  │
└────────┴────────┴────────┴────────┘
```

### Instrucciones Implementadas

#### LD (Load) - Opcode 0x01

Carga un valor de memoria a registro:

```c
LD Rd, Rs, Imm
// Rd = Memory[Rs + Imm]
```

**Ejemplo:**
```
LD R1, R0, 4   // R1 = Memory[R0 + 4]
```

**Implementación:**
```c
case OP_LD:
    addr = mmu->registers[rs] + imm;
    physical_addr = mmu_translate(mmu, pm, addr, 0);  // Read
    if (physical_addr == UINT32_MAX) {
        LOG_ERROR(LOG_COMPONENT_MEMORY, "LD page fault");
        return -1;
    }
    mmu->registers[rd] = memory_read_word(pm, physical_addr);
    break;
```

#### ST (Store) - Opcode 0x02

Almacena un valor de registro en memoria:

```c
ST Rs, Rd, Imm
// Memory[Rd + Imm] = Rs
```

**Ejemplo:**
```
ST R1, R0, 8   // Memory[R0 + 8] = R1
```

**Implementación:**
```c
case OP_ST:
    addr = mmu->registers[rd] + imm;
    physical_addr = mmu_translate(mmu, pm, addr, 1);  // Write
    if (physical_addr == UINT32_MAX) {
        LOG_ERROR(LOG_COMPONENT_MEMORY, "ST page fault");
        return -1;
    }
    memory_write_word(pm, physical_addr, mmu->registers[rs]);
    break;
```

#### ADD - Opcode 0x03

Suma dos registros:

```c
ADD Rd, Rs, Imm
// Rd = Rs + Imm
```

**Ejemplo:**
```
ADD R2, R1, 5   // R2 = R1 + 5
```

**Implementación:**
```c
case OP_ADD:
    mmu->registers[rd] = mmu->registers[rs] + (int8_t)imm;
    break;
```

#### EXIT - Opcode 0xFF

Termina el proceso:

```c
EXIT
```

**Implementación:**
```c
case OP_EXIT:
    LOG_INFO(LOG_COMPONENT_MACHINE, "Process EXIT");
    return 1;  // Señal de terminación
```

### Motor de Ejecución

El motor ejecuta instrucciones en cada tick del clock:

```c
int instruction_execute(MMU* mmu, PhysicalMemory* pm)
{
    // 1. Fetch: Leer instrucción en PC
    uint32_t physical_pc = mmu_translate(mmu, pm, mmu->pc, 0);
    if (physical_pc == UINT32_MAX) return -1;
    
    uint32_t instruction = memory_read_word(pm, physical_pc);
    mmu->ir = instruction;
    
    // 2. Decode: Extraer campos
    uint8_t opcode = (instruction >> 24) & 0xFF;
    uint8_t rd = (instruction >> 16) & 0xFF;
    uint8_t rs = (instruction >> 8) & 0xFF;
    uint8_t imm = instruction & 0xFF;
    
    // 3. Execute: Ejecutar según opcode
    int result = 0;
    switch (opcode) {
        case OP_LD:   /* ... */ break;
        case OP_ST:   /* ... */ break;
        case OP_ADD:  /* ... */ break;
        case OP_EXIT: result = 1; break;
        default:
            LOG_ERROR(LOG_COMPONENT_MACHINE, 
                     "Unknown opcode 0x%X", opcode);
            return -1;
    }
    
    // 4. Update PC
    if (result != 1) {  // Si no es EXIT
        mmu->pc += 4;   // Siguiente instrucción
    }
    
    return result;
}
```

## Loader de Programas

### Formato .elf

Los archivos `.elf` contienen secciones de código y datos:

```
.elf file format:
┌──────────────────────────────────────┐
│ [.text]                               │
│ <size>                                │
│ <instruction_1>                       │
│ <instruction_2>                       │
│ ...                                   │
├──────────────────────────────────────┤
│ [.data]                               │
│ <size>                                │
│ <data_1>                              │
│ <data_2>                              │
│ ...                                   │
└──────────────────────────────────────┘
```

### Proceso de Carga

```c
int loader_load_program(const char* filename, PCB* pcb, 
                       PhysicalMemory* pm)
{
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    
    char line[256];
    int in_text = 0, in_data = 0;
    
    // 1. Crear Page Table para el proceso
    int pt_frame = physical_memory_allocate_frame(pm);
    pcb->mm.pgb = pt_frame * PAGE_SIZE;
    PageTable* pt = (PageTable*)(pm->data + pcb->mm.pgb);
    memset(pt, 0, sizeof(PageTable));
    
    // 2. Definir layout
    pcb->mm.code_start = 0x0;
    pcb->mm.data_start = 0x10000;  // 64KB después
    
    uint32_t code_offset = 0;
    uint32_t data_offset = 0;
    
    // 3. Parsear archivo
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "[.text]")) {
            in_text = 1; in_data = 0;
            continue;
        }
        if (strstr(line, "[.data]")) {
            in_text = 0; in_data = 1;
            continue;
        }
        
        if (in_text) {
            // Parsear instrucción y escribir en memoria
            uint32_t instruction = parse_instruction(line);
            uint32_t vaddr = pcb->mm.code_start + code_offset;
            
            // Asignar página si es necesario
            ensure_page_allocated(pm, pt, vaddr);
            
            // Escribir instrucción
            uint32_t paddr = translate_address(pm, pt, vaddr);
            memory_write_word(pm, paddr, instruction);
            
            code_offset += 4;
        }
        
        if (in_data) {
            // Similar para datos
        }
    }
    
    pcb->mm.code_size = code_offset;
    pcb->mm.data_size = data_offset;
    pcb->is_loaded = 1;
    
    fclose(f);
    return 0;
}
```

### Integración con Scheduler

Cuando se despacha un proceso con programa cargado:

```c
static void scheduler_dispatch(HWThread* thread, PCB* pcb, 
                               uint32_t cpu, uint32_t core, 
                               uint32_t hw_thread)
{
    pcb->state = PROCESS_STATE_RUNNING;
    thread->current_pcb = pcb;
    
    // Configurar MMU si hay programa cargado
    if (pcb->is_loaded && thread->mmu) {
        mmu_set_ptbr(thread->mmu, pcb->mm.pgb);
        thread->mmu->pc = pcb->mm.code_start;
    }
}
```

## Prometheus - Generador de Programas

### Diseño

Prometheus es una herramienta que genera archivos `.elf` aleatorios para testing. Utiliza un generador de números pseudoaleatorios con semilla configurable para reproducibilidad.

### Uso

```bash
# Generar archivos por defecto (60 programas)
make elfs

# Uso manual
./build/prometheus -s 42 -nprog -f0 -l20 -p100
```

**Parámetros:**
- `-s`: Semilla (default: 0)
- `-n`: Prefijo de nombre (default: "prog")
- `-f`: Primer número (default: 0)
- `-l`: Líneas aproximadas (default: 20)
- `-p`: Cantidad de programas (default: 50)

### Algoritmo de Generación

```c
void generate_program(FILE* f, int lines)
{
    fprintf(f, "[.text]\n");
    fprintf(f, "%d\n", lines);
    
    // Generar instrucciones aleatorias
    for (int i = 0; i < lines - 1; i++) {
        int opcode = rand() % 3;  // LD, ST o ADD
        int rd = rand() % 16;
        int rs = rand() % 16;
        int imm = rand() % 256;
        
        switch (opcode) {
            case 0: fprintf(f, "LD R%d, R%d, %d\n", rd, rs, imm); break;
            case 1: fprintf(f, "ST R%d, R%d, %d\n", rs, rd, imm); break;
            case 2: fprintf(f, "ADD R%d, R%d, %d\n", rd, rs, imm); break;
        }
    }
    
    // Última instrucción: EXIT
    fprintf(f, "EXIT\n");
    
    // Generar sección .data
    fprintf(f, "[.data]\n");
    int data_size = rand() % 10 + 5;
    fprintf(f, "%d\n", data_size);
    
    for (int i = 0; i < data_size; i++) {
        fprintf(f, "%d\n", rand() % 1000);
    }
}
```

### Reproducibilidad

El `Makefile` usa semilla fija para garantizar que todos generen los mismos archivos:

```makefile
elfs: prometheus
	@mkdir -p elfs
	./build/prometheus -s 42 -nprog -f0 -l20 -p60
```

Esto facilita debugging colaborativo: todos los desarrolladores trabajan con los mismos programas de prueba.

## Testing de Memoria Virtual

### Tests Implementados

1. **TLB Hit Rate**: Verificar que el TLB reduce accesos a memoria
2. **Page Translation**: Validar traducción correcta virtual→física
3. **Program Loading**: Comprobar carga de archivos `.elf`
4. **Instruction Execution**: Validar ejecución de LD, ST, ADD, EXIT
5. **Memory Isolation**: Verificar que procesos no acceden memoria ajena

### Ejemplo de Ejecución

```bash
# Cargar y ejecutar programa
$ ./build/churros -c 1 -o 1 -t 1 -d 100

[INFO] [Loader] Loading program: elfs/prog000.elf
[INFO] [Loader] Code section: 20 instructions
[INFO] [Loader] Data section: 8 words
[DEBUG] [Memory] TLB miss - VPN=0x0 → PFN=0x100
[DEBUG] [Memory] TLB hit - VPN=0x0 (hit rate: 90.5%)
[INFO] [Machine] Executing: LD R1, R0, 4
[INFO] [Machine] Executing: ADD R2, R1, 5
[INFO] [Machine] Executing: ST R2, R0, 8
...
[INFO] [Machine] Process EXIT
```

### Validación de TLB

El sistema reporta estadísticas de TLB al finalizar:

```bash
$ ./build/churros -l debug -d 50

TLB Statistics:
  Hits: 1245
  Misses: 156
  Hit Rate: 88.9%
```

Un hit rate > 85% indica buen funcionamiento del TLB.

## Optimizaciones

### 1. TLB Flush al Context Switch

Cuando cambia el proceso, se invalida el TLB:

```c
void mmu_flush_tlb(MMU* mmu)
{
    for (uint32_t i = 0; i < TLB_SIZE; i++) {
        mmu->tlb.entries[i].valid = 0;
    }
}
```

Esto evita traducciones incorrectas entre procesos.

### 2. Asignación Lazy de Páginas

Las páginas se asignan solo cuando se acceden por primera vez (no implementado, pero diseñado para futuro).

### 3. Bitmap para Frame Allocation

El allocator usa un bitmap simple en lugar de lista enlazada:

```c
int physical_memory_allocate_frame(PhysicalMemory* pm)
{
    pthread_mutex_lock(&pm->mutex);
    
    for (int i = KERNEL_RESERVED_PAGES; i < NUM_PAGES; i++) {
        if (pm->free_frames[i]) {
            pm->free_frames[i] = 0;  // Marcar ocupado
            pthread_mutex_unlock(&pm->mutex);
            return i;
        }
    }
    
    pthread_mutex_unlock(&pm->mutex);
    return -1;  // Sin frames libres
}
```

Complejidad: O(n), pero simple y suficiente para simulador educativo.

## Limitaciones y Trabajo Futuro

### Limitaciones Actuales

1. **Sin swapping**: Todas las páginas residen en memoria física
2. **Sin page faults reales**: No hay manejo de páginas no presentes
3. **Instruction set limitado**: Solo LD, ST, ADD, EXIT
4. **Sin protección de memoria**: No hay bits de permisos (R/W/X)
5. **Sin memoria compartida**: Cada proceso tiene espacio aislado

### Mejoras Futuras

**Parte 4: Instrucciones Avanzadas**
- Branches condicionales (BEQ, BNE)
- Multiplicación/División
- Operaciones lógicas (AND, OR, XOR)
- Shifts y rotaciones

**Parte 5: Page Faults y Swapping**
- Algoritmos de reemplazo (LRU, Clock)
- Swap space en disco simulado
- Manejo de page faults
- Working set tracking

**Parte 6: Sistema de Archivos**
- VFS (Virtual File System)
- Inodos y directorios
- File descriptors
- System calls (open, read, write, close)

## Conclusiones de la Parte 3

La implementación de gestión de memoria virtual demuestra:

**Paginación completa**: Sistema de paginación de 4KB funcional  
**MMU con TLB**: Traducción de direcciones con cache  
**Loader funcional**: Carga de programas desde archivos `.elf`  
**Instruction set básico**: Ejecución de LD, ST, ADD, EXIT  
**Prometheus**: Generador de programas de prueba reproducibles  
**Integración con scheduler**: Cambio de contexto incluye cambio de PTBR  
**Estadísticas de rendimiento**: TLB hit rate, page faults, etc.  

El sistema simula fielmente el funcionamiento de un MMU real y proporciona una base sólida para extensiones futuras (swapping, protección, memoria compartida).

\newpage
