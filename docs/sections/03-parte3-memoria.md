# Parte 3: Gestión de Memoria Virtual

## Introducción

La tercera parte cierra el círculo: memoria virtual completa. Sobre lo que ya tenía (Parte 1 y 2), he añadido `memory.c/h`, `loader.c/h` e `instruction.c/h`, más un generador de programas que he llamado `prometheus`.

Qué he implementado:

- **MMU** (Memory Management Unit) con traducción virtual $\rightarrow$ física
- **TLB** (Translation Lookaside Buffer) de 16 entradas para cachear traducciones
- **Paginación** de 4KB con tabla de páginas por proceso
- **Loader** de programas en formato texto plano con asignación dinámica de frames
- **ISA** básico con instrucciones LOAD, STORE, ADD, EXIT

**Nota sobre "ELF"**: Aunque llamo a los archivos `.elf` y al componente "loader ELF", **NO uso el formato binario ELF estándar**. Uso archivos de texto plano con directivas `.text` y `.data` seguidas de instrucciones en hexadecimal. He mantenido el nombre ".elf" por consistencia con el enunciado, pero técnicamente es un formato custom.

## Arquitectura de Memoria

Tengo 16MB de direcciones (24 bits), divididos así:

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

Recorre el bitmap desde `kernel_end_frame` buscando el primer bit a 0. Lo setea a 1, actualiza contadores y devuelve el índice del frame. Si no hay frames libres devuelve 0xFFFFFFFF.

## TLB (Translation Lookaside Buffer)

El TLB es una caché asociativa de 16 entradas que guarda traducciones VPN $\rightarrow$ PFN recientes. Reduce un montón los accesos a la Page Table —en mis pruebas he visto mejoras de $6\times$ a $10\times$.

### Estructura

```c
typedef struct {
    uint32_t vpn;        // Virtual Page Number
    uint32_t pfn;        // Physical Frame Number
    int valid;           // Entrada válida
    uint64_t last_used;  // Timestamp para LRU
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_SIZE];  // 16 entradas
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

### Búsqueda y Reemplazo

**lookup(vpn):** Recorre las 16 entradas buscando `valid` $=1$ y `vpn` $=$ `input`. Si encuentra, actualiza `last_used` incrementando el `access_counter` global e incrementa hits. Si no encuentra, incrementa misses.

**update(vpn, pfn):** Primero busca entradas inválidas (`valid=0`). Si no hay, encuentra la entrada con menor `last_used` (política LRU - Least Recently Used) y la reemplaza. Setea `vpn`, `pfn`, `valid=1` y actualiza `last_used` con el contador actual.

## Page Table

Cada proceso tiene su propia tabla de páginas que mapea VPN $\rightarrow$ PFN.

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

No asignamos todas las páginas al cargar el programa —¿para qué? Usamos demand paging: cuando se accede a una página no mapeada, `page_table_map()` pide un frame físico con `allocate_frame()`, setea la entrada `{pfn, valid=1, present=1}` y listo.

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

```{.mermaid format=pdf}
flowchart TD
    START([vaddr]) --> SPLIT[Extraer VPN y offset]
    SPLIT --> TLB{TLB lookup}
    TLB -->|HIT| COMPOSE1["pfn << 12 | offset"]
    TLB -->|MISS| PT{Page Table lookup}
    PT -->|Valid| UPDATE[tlb_update]
    PT -->|Invalid| MAP[page_table_map]
    MAP --> UPDATE
    UPDATE --> COMPOSE2["pfn << 12 | offset"]
    COMPOSE1 --> END([paddr])
    COMPOSE2 --> END
```

Paso a paso: (1) extraer VPN y offset, (2) consultar TLB, (3) si miss: consultar Page Table, (4) si inválida: asignar bajo demanda, (5) actualizar TLB, (6) componer dirección física.

**read_word/write_word:** Traducen con `mmu_translate()` y llaman a `physical_memory_read/write_word()`.

### Ejemplo de Traducción

Traducción de dirección virtual `0x001234` paso a paso:

```plaintext
1. Dirección virtual: 0x001234
   ┌──────────────┬───────────────┐
   │  VPN = 0x001 │ Offset = 0x234│
   └──────────────┴───────────────┘
     bits 23-12      bits 11-0
   
2. TLB Lookup (VPN=0x001):
   - Buscar en 16 entradas
   - MISS (primera vez)
   
3. Page Table Lookup (VPN=0x001):
   - Buscar entries[0x001]
   - INVALID → Asignar bajo demanda
   
4. Allocate Frame:
   - physical_memory_allocate_frame()
   - Frame asignado: PFN=0x100
   - Actualizar PT: entries[0x001] = {pfn:0x100, valid:1}
   
5. Update TLB:
   - Buscar entrada inválida o usar LRU
   - Insertar {vpn:0x001, pfn:0x100, valid:1, last_used:counter++}
   
6. Componer dirección física:
   paddr = (PFN << 12) | offset
   paddr = (0x100 << 12) | 0x234
   paddr = 0x100000 | 0x234
   paddr = 0x100234
   
Resultado: 0x001234 → 0x100234
```

**Siguiente acceso a 0x001240:**
```plaintext
VPN=0x001 → TLB HIT! → PFN=0x100
paddr = 0x100240 (sin acceso a Page Table)
```

## Conjunto de Instrucciones (ISA)

Para validar que la memoria virtual funciona, he implementado un ISA minimalista con 4 instrucciones. No hace falta más:

### Formato de Instrucción

Cada instrucción ocupa exactamente 32 bits (4 bytes, 1 word), permitiendo fetch atómico con una sola lectura de memoria. El opcode está en los 4 bits más significativos (bits 31-28), permitiendo hasta 16 instrucciones diferentes.

Instrucciones tipo I (LOAD/STORE) usan: opcode (4 bits) + registro (4 bits) + address (24 bits). La dirección de 24 bits permite acceder a todo el espacio de direcciones virtuales (16MB). El campo de registro (Rd para LOAD, Rs para STORE) identifica qué registro usar.

Instrucciones tipo R (ADD) usan: opcode (4 bits) + Rd (4 bits) + Rs1 (4 bits) + Rs2 (4 bits) + unused (16 bits). Los tres campos de registro permiten operaciones triádicas: `Rd = Rs1 + Rs2`.

EXIT es especial: opcode 0xF (bits 31-28) + 28 bits ignorados. Detectar EXIT es trivial: `if ((instr >> 28) == 0xF)`. No necesita operandos porque su única función es terminar el proceso.

Esta codificación compacta permite programas pequeños que caben en pocas páginas de memoria, facilitando las pruebas del sistema de paginación sin desperdiciar frames.

```plaintext
LOAD/STORE (tipo I):
┌──────┬──────┬────────────────────────┐
│opcode│  Rd  │       address          │
└──────┴──────┴────────────────────────┘
 31  28 27  24 23                     0

ADD (tipo R):
┌──────┬──────┬──────┬──────┬────────┐
│opcode│  Rd  │ Rs1  │ Rs2  │ unused │
└──────┴──────┴──────┴──────┴────────┘
 31  28 27  24 23  20 19  16 15      0

EXIT:
┌──────┬──────────────────────────────┐
│ 0xF  │          unused              │
└──────┴──────────────────────────────┘
 31  28 27                           0
```

**Ejemplos codificados:**
```c
0x0A100020  // LD r10, 0x100020 (opcode=0x0, Rd=0xA, addr=0x100020)
0x1C100028  // ST r12, 0x100028 (opcode=0x1, Rs=0xC, addr=0x100028)
0x2CAB0000  // ADD r12, r10, r11 (opcode=0x2, Rd=0xC, Rs1=0xA, Rs2=0xB)
0xF0000000  // EXIT (opcode=0xF)
```

### Instrucciones

| Opcode | Mnemónico | Descripción |
|--------|-----------|-------------|
| 0x0    | `LD Rd, addr` | Rd ← memoria[addr] |
| 0x1    | `ST Rs, addr` | memoria[addr] ← Rs |
| 0x2    | `ADD Rd, Rs1, Rs2` | Rd $\leftarrow$ Rs1 + Rs2 |
| 0xF    | `EXIT` | Terminar proceso |

La ejecución hace switch sobre opcode extraído con `GET_OPCODE()`, llama a `mmu_read_word/write_word` para accesos a memoria, actualiza registros de la MMU, y devuelve 1 para continuar o 0 si encuentra EXIT.

## Loader de Programas

El Loader carga programas desde archivos de texto plano y crea procesos con memoria virtual inicializada. **No usamos ELF binarios reales** sino un formato texto simplificado compatible con Prometheus.

### Formato de Archivo (Texto Plano)

Los archivos `.elf` generados por Prometheus tienen formato texto:

```plaintext
.text <dirección_inicio_hex>
.data <dirección_inicio_hex>
<instrucción_1_hex>
<instrucción_2_hex>
...
<instrucción_EXIT>
<dato_1_hex>
<dato_2_hex>
...
```

**Ejemplo real** (`prog001.elf`):

```plaintext
.text 100000
.data 100020
0A100020
0B100024
2CAB0000
1C100028
F0000000
00000005
0000000A
```

- Primera línea: segmento .text empieza en `0x100000`
- Segunda línea: segmento .data empieza en `0x100020`  
- Líneas 3-7: instrucciones en hex (terminan con EXIT `0xF0000000`)
- Líneas 8-9: datos iniciales en hex

### Carga de Programas

`loader_load_program()` realiza los siguientes pasos:

1. **Parsear archivo**: Lee headers `.text` y `.data`, separa instrucciones de datos
2. **Crear PCB**: Inicializa proceso con PID único, estado NEW, temperatura 0
3. **Crear Page Table**: Asigna tabla de páginas nueva en memoria física
4. **Cargar .text**: Para cada instrucción, calcula VPN de su dirección virtual, asigna frame si es página nueva, escribe la instrucción con `physical_memory_write_word()`
5. **Cargar .data**: Mismo proceso pero con datos iniciales
6. **Configurar MMU**: Setea PTBR al physical address de la Page Table y PC al inicio de .text

El loader **no** asigna todas las páginas al cargar—solo las que contienen código o datos iniciales. El resto se asignan bajo demanda cuando el programa las accede.

### Generador de Programas (Prometheus)

Para las pruebas creamos `prometheus`, un generador que produce programas sintéticos con patrones controlables de acceso a memoria. Genera secuencias de LOAD/STORE/ADD/EXIT con direcciones distribuidas para forzar múltiples páginas y estresar el TLB.

#### Funcionamiento

Prometheus toma varios parámetros:

- `-s <seed>`: Semilla para generación aleatoria (reproducibilidad)
- `-n <num>`: Cuántos programas generar
- `-f <first>`: Número del primer programa (para nombrado)
- `-l <max_lines>`: Número máximo de instrucciones por programa
- `-p <max_lines_data>`: Número máximo de datos en segmento .data

**Ejemplo de uso:**
```bash
# Generar 10 programas con semilla 100, entre 5-20 instrucciones
./build/prometheus -s 100 -n 10 -f 0 -l 20 -p 60
```

Cada programa generado sigue el patrón de 4 instrucciones repetido en bucle:

1. **LOAD** de variable aleatoria del segmento .data → registro Rd
2. **LOAD** de otra variable .data → registro Rd+1
3. **ADD** de ambos registros → registro Rd+2
4. **STORE** del resultado en .data → variable siguiente

Este patrón genera un patrón de acceso a memoria realista: lecturas de datos, procesamiento (suma), y escritura de resultados. Las direcciones están distribuidas aleatoriamente en el segmento .data, forzando al TLB a trabajar con múltiples páginas.

#### Formato de Salida

```plaintext
.text 100000
.data 100020
0A100020
0B100024
2CAB0000
1C100028
F0000000
00000005
0000000A
```

- **Línea 1**: `.text <hex>` — dirección de inicio del código (siempre alineada a límite de página)
- **Línea 2**: `.data <hex>` — dirección de inicio de datos (calculada como .text + tamaño_código alineado)
- **Líneas 3-(n-1)**: Instrucciones codificadas en hex (LOAD/STORE/ADD)
- **Línea n**: `F0000000` — instrucción EXIT que termina el programa
- **Líneas (n+1)+**: Valores de datos iniciales en hex (típicamente entre 4-60 words)

Los datos iniciales son valores aleatorios entre 0 y 100, simulando variables ya inicializadas en memoria.

```bash
$ ./prometheus/heracles -n 10 -s 100
Generated 10 ELF programs in elfs/ directory
```

#### Formato de Archivo

Los programas usan formato texto plano (no ELF binario real):

```plaintext
.text 00001000
.data 00002000
10001000
10002004
30000005
F0000000
0000002A
```

- Línea 1: `.text <dirección>` — inicio del segmento de código
- Línea 2: `.data <dirección>` — inicio del segmento de datos
- Siguientes líneas: instrucciones en hexadecimal (formato compactado opcode+operando)
- Última instrucción: `EXIT` (0xF0000000)
- Después de EXIT: valores de datos iniciales

Cada programa tiene entre 5 y 50 instrucciones, con direcciones virtuales que fuerzan el uso de múltiples páginas (validando paginación y TLB).

## Integración con el Kernel

En cada tick, `machine_advance_cycle()` ejecuta un ciclo completo de CPU para procesos con programa cargado. Este ciclo refleja el comportamiento de CPUs reales pero simplificado.

### Ciclo Fetch-Decode-Execute

```{.mermaid format=pdf}
flowchart TD
    Start([Inicio Tick]) --> CheckLoaded{Proceso tiene\nprograma?}
    CheckLoaded -->|No| DecrTTL[TTL--]
    DecrTTL --> End([Fin Tick])
    
    CheckLoaded -->|Sí| Fetch
    
    subgraph Fetch ["FETCH"]
        F1[Leer PC de MMU]
        F2[mmu_translate PC]
        F3{TLB?}
        F3 -->|HIT| F4a[PFN from TLB]
        F3 -->|MISS| F4b[Page Table lookup]
        F4b -->|Invalid| F5[Allocate frame]
        F5 --> F6[Update TLB]
        F4b -->|Valid| F6
        F4a --> F7
        F6 --> F7[physical_memory_read_word]
        F7 --> F8[Guardar en IR]
    end
    
    Fetch --> Decode
    
    subgraph Decode ["DECODE"]
        D1[GET_OPCODE instr]
        D2{Tipo?}
        D2 -->|LD/ST| D3[GET_RD + GET_ADDR]
        D2 -->|ADD| D4[GET_RD + GET_RS1 + GET_RS2]
        D2 -->|EXIT| D5[Sin operandos]
    end
    
    Decode --> Execute
    
    subgraph Execute ["EXECUTE"]
        E1{Opcode?}
        E1 -->|LOAD| E2[Traducir addr\nLeer memoria\nRegistro = valor]
        E1 -->|STORE| E3[Traducir addr\nvalor = Registro\nEscribir memoria]
        E1 -->|ADD| E4[Rd = Rs1 + Rs2]
        E1 -->|EXIT| E5[state = TERMINATED\nreturn 0]
    end
    
    Execute --> UpdatePC{EXIT?}
    UpdatePC -->|No| PC[PC += 4]
    UpdatePC -->|Sí| Terminate[Terminar proceso]
    PC --> End
    Terminate --> End
    
    style Fetch fill:#e1f5ff
    style Decode fill:#ffe1f5
    style Execute fill:#e1ffe1
```

**Fetch:** Se lee el PC (Program Counter) de la MMU, que contiene la dirección virtual de la próxima instrucción. Se traduce con `mmu_translate()` obteniendo la dirección física (posible TLB hit/miss y page fault). Se lee la palabra de memoria física con `physical_memory_read_word()` y se guarda en el IR (Instruction Register) de la MMU.

**Decode:** Se extrae el opcode usando `GET_OPCODE(instr)` que aplica máscara y shift: `(instr >> 28) & 0xF`. Según el opcode, se extraen los demás campos: registro destino `GET_RD()`, dirección `GET_ADDR()`, o registros fuente `GET_RS1()/GET_RS2()`. Estos macros encapsulan el bit-twiddling necesario.

**Execute:** Se ejecuta la operación correspondiente. LOAD traduce la dirección operando (segunda traducción), lee memoria y guarda en registro. STORE traduce dirección, lee registro y escribe memoria. ADD suma inmediato a registro. EXIT setea el estado del proceso a TERMINATED y retorna 0, deteniendo la ejecución.

**Update PC:** Si la instrucción no fue EXIT (retornó 1), se incrementa PC += 4 (WORD_SIZE) apuntando a la siguiente instrucción. El ciclo se repite en el próximo tick.

Procesos sin programa (`is_loaded=false`) omiten este ciclo y solo decrementan TTL, simulando procesos que no ejecutan código real pero consumen tiempo de CPU. Esto permite mezclar procesos reales con procesos sintéticos en las pruebas.

## Resultados de Pruebas

**TLB Hit Rate**: 85-95% en las pruebas, validando localidad espacial/temporal.

**Uso de Memoria**: Demand paging funciona correctamente. Cada proceso usa 1-2 frames (según páginas accedidas).

**Trazas**: Primer acceso a VPN $\\rightarrow$ TLB MISS + asignación de frame. Accesos subsiguientes $\\rightarrow$ TLB HIT. Instrucciones ejecutadas correctamente, traducciones virtuales $\\rightarrow$ físicas consistentes.

## Decisiones de Diseño

### Tamaño de Página 4KB

Usamos 4KB como tamaño de página, el estándar en arquitecturas x86/x64. Equilibra fragmentación interna (páginas grandes desperdician memoria) y overhead de Page Table (páginas pequeñas requieren más entradas).

### TLB de 16 Entradas

Un TLB de 16 entradas es pequeño comparado con hardware real (~512-1024 entradas en CPUs modernos), pero suficiente para mi simulador. Me ha permitido observar tanto HITs como MISSes en las trazas, validando la política de reemplazo LRU. Con programas que acceden a 3-5 páginas diferentes, un TLB de 16 entradas mantiene todas las traducciones activas en caché, explicando los hit rates altos ($85\%-95\%$).

### MMU por Hardware Thread

Cada hardware thread tiene su propia MMU independiente con TLB privado. Esta decisión de diseño refleja arquitecturas reales donde cada contexto de ejecución mantiene su propio TLB para evitar interferencias entre procesos.

Al despachar un proceso, el scheduler configura la MMU del thread de destino. La operación crítica es `mmu_set_ptbr(pcb->mm.pgb)`, que actualiza el Page Table Base Register apuntando a la tabla de páginas del proceso nuevo. Este cambio de puntero modifica instantáneamente el espacio de direcciones visible: las mismas direcciones virtuales ahora se traducen a frames físicos completamente diferentes.

Al cambiar el PTBR, el TLB queda automáticamente inválido porque sus entradas apuntan a traducciones del proceso anterior. La implementación hace flush explícito con `tlb_invalidate()`, marcando todas las entradas como invalid=0. El proceso nuevo empieza con TLB frío, sufriendo misses hasta que se calientan las traducciones más usadas.

El PC (Program Counter) también se actualiza a `pcb->mm.code_start`, posicionando la ejecución al inicio del segmento de código del proceso. La primera instrucción fetcheada desencadena traducciones de dirección y posiblemente page faults si el código no está mapeado aún (demand paging).

Este modelo permite context switches ultra-rápidos: solo cambiamos dos punteros (PTBR y PC) y limpiamos el TLB. No hay que copiar ni reconstruir estructuras, todo queda ready para ejecutar inmediatamente.

### Demand Paging

Las páginas se asignan bajo demanda (no todas al cargar el programa). Esta decisión refleja el comportamiento de sistemas operativos reales y optimiza el uso de memoria: solo se asignan las páginas realmente accedidas. El loader solo mapea las páginas que contienen código o datos iniciales; el resto se asignan en el primer acceso (page fault transparente).

### ISA Minimalista

Limitamos el conjunto de instrucciones a 4 opcodes para mantener la simplicidad. Aunque limitado, es suficiente para validar todos los aspectos de la memoria virtual: lectura (LOAD), escritura (STORE), procesamiento (ADD) y terminación (EXIT). Añadir más instrucciones no aportaría valor a la validación del subsistema de memoria.

### Context Switches y MMU

Al despachar un proceso a un hardware thread, el scheduler configura la MMU del thread con el espacio de direcciones del proceso:

```{.mermaid format=pdf}
sequenceDiagram
    participant S as Scheduler
    participant T as HW Thread
    participant M as MMU
    participant P as PCB
    
    S->>T: scheduler_dispatch(pcb)
    T->>P: Actualizar estado (RUNNING)
    T->>P: Asignar IDs (cpu, core, thread)
    T->>P: ticks_since_swap = 0
    
    alt Proceso con programa cargado
        T->>M: mmu_set_ptbr(pcb->mm.pgb)
        Note over M: Cambiar espacio direcciones
        T->>M: pc = pcb->mm.code_start
        M->>M: Invalidar TLB
        Note over M: TLB flush automático
    end
    
    T->>T: current_pcb = pcb
```

Esta arquitectura modela hardware real donde cada core tiene su MMU y TLB. El PTBR permite cambiar de espacio de direcciones sin reconstruir la MMU. El TLB se invalida automáticamente al cambiar PTBR, forzando al nuevo proceso a reconstruir sus traducciones en caché.

\newpage
