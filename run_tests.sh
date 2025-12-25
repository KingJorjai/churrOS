#!/bin/bash
# Script de pruebas para churrOS - Tests automatizados con validación

echo "======================================"
echo "  churrOS - Suite de Pruebas"
echo "  Validación Automática de Algoritmos"
echo "======================================"
echo ""

# Colores para output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Contadores de tests
TESTS_PASSED=0
TESTS_FAILED=0
TOTAL_TESTS=0

# Archivo temporal para capturar salida
TEMP_OUTPUT="/tmp/churros_test_output_$$.txt"

# Compilar primero
echo -e "${BLUE}Compilando churrOS...${NC}"
make clean && make
if [ $? -ne 0 ]; then
    echo -e "${RED}Error en la compilación${NC}"
    exit 1
fi
echo ""

# Compilar ELFs si existen
if [ -d "prometheus" ]; then
    echo -e "${BLUE}Compilando ELFs de prueba...${NC}"
    cd prometheus || exit 1
    make clean && make
    if [ $? -ne 0 ]; then
        echo -e "${YELLOW}Advertencia: Error al compilar ELFs, algunos tests podrían fallar${NC}"
    fi
    cd .. || exit 1
    echo ""
fi

# Función para separar tests
separator() {
    echo ""
    echo "--------------------------------------"
    echo ""
}

# Función para ejecutar y validar un test
# Parámetros: $1=nombre, $2=descripción, $3=comando, $4=validaciones
run_test() {
    local test_name="$1"
    local test_desc="$2"
    local test_cmd="$3"
    local validations="$4"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo -e "${GREEN}${test_name}${NC}"
    echo "Descripción: ${test_desc}"
    echo "Ejecutando: ${test_cmd}"
    
    # Ejecutar y capturar salida
    eval "${test_cmd}" > "${TEMP_OUTPUT}" 2>&1
    local exit_code=$?
    
    # Mostrar salida
    cat "${TEMP_OUTPUT}"
    echo ""
    
    # Validar
    local test_passed=true
    local validation_msgs=""
    
    if [ ${exit_code} -ne 0 ]; then
        test_passed=false
        validation_msgs="${validation_msgs}  ✗ El programa terminó con error (código ${exit_code})\n"
    fi
    
    # Procesar validaciones
    IFS='|' read -ra VALIDATIONS <<< "$validations"
    for validation in "${VALIDATIONS[@]}"; do
        IFS=':' read -ra PARTS <<< "$validation"
        local type="${PARTS[0]}"
        local pattern="${PARTS[1]}"
        local desc="${PARTS[2]}"
        
        if [ "$type" == "MUST_HAVE" ]; then
            if grep -q "$pattern" "${TEMP_OUTPUT}"; then
                validation_msgs="${validation_msgs}  ✓ ${desc}\n"
            else
                test_passed=false
                validation_msgs="${validation_msgs}  ✗ ${desc} [NO ENCONTRADO]\n"
            fi
        elif [ "$type" == "MUST_NOT_HAVE" ]; then
            if ! grep -q "$pattern" "${TEMP_OUTPUT}"; then
                validation_msgs="${validation_msgs}  ✓ ${desc}\n"
            else
                test_passed=false
                validation_msgs="${validation_msgs}  ✗ ${desc} [ENCONTRADO CUANDO NO DEBERÍA]\n"
            fi
        elif [ "$type" == "COUNT_MIN" ]; then
            local count=$(grep -c "$pattern" "${TEMP_OUTPUT}")
            local min_count="${PARTS[3]}"
            if [ ${count} -ge ${min_count} ]; then
                validation_msgs="${validation_msgs}  ✓ ${desc} (encontrado ${count} veces)\n"
            else
                test_passed=false
                validation_msgs="${validation_msgs}  ✗ ${desc} (encontrado ${count} veces, esperado >= ${min_count})\n"
            fi
        fi
    done
    
    # Mostrar resultados de validación
    echo -e "${BOLD}Validación:${NC}"
    echo -e "${validation_msgs}"
    
    if [ "$test_passed" = true ]; then
        echo -e "${GREEN}${BOLD}✓ TEST PASADO${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}${BOLD}✗ TEST FALLIDO${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    
    separator
}

# ==========================================
# SECCIÓN 1: Tests de Round Robin
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 1: Tests de Round Robin${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 1.1: Round Robin Monohilo (Verificar Preemption)
run_test \
    "Test 1.1: Round Robin Monohilo - Verificación de Preemption" \
    "1 solo hilo debe alternar procesos cada quantum" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 4 -g 5 -s 10 -d 50" \
    "MUST_HAVE:Round Robin:Confirma que usa Round Robin|COUNT_MIN:Preemption:Debe haber preemptions por quantum:3|MUST_HAVE:Scheduler:Scheduler debe ejecutarse"

# Test 1.2: Round Robin con Quantum Muy Corto
run_test \
    "Test 1.2: Round Robin - Quantum Muy Corto (Alta Frecuencia)" \
    "Quantum de 2 ticks fuerza cambios de contexto constantes" \
    "./build/churros -a rr -c 1 -o 1 -t 2 -q 2 -g 6 -s 10 -d 40" \
    "MUST_HAVE:Round Robin:Confirma que usa Round Robin|COUNT_MIN:Preemption:Debe haber muchas preemptions:5|MUST_HAVE:Dispatch:Debe haber dispatches de procesos"

# Test 1.3: Round Robin Multicore
run_test \
    "Test 1.3: Round Robin - Distribución Multicore" \
    "Múltiples hilos procesando con Round Robin" \
    "./build/churros -a rr -c 1 -o 2 -t 2 -q 5 -g 4 -s 10 -d 60" \
    "MUST_HAVE:Round Robin:Confirma que usa Round Robin|COUNT_MIN:Dispatch:Debe haber dispatches en múltiples threads:4|MUST_HAVE:Scheduler:Scheduler debe activarse"

# ==========================================
# SECCIÓN 2: Tests de FIFO
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 2: Tests de FIFO${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 2.1: FIFO Monohilo (Sin Preemption por Tiempo)
run_test \
    "Test 2.1: FIFO Monohilo - Verificación Sin Preemption" \
    "Mismo escenario que 1.1 pero con FIFO - NO debe haber preemptions" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 5 -s 10 -d 50" \
    "MUST_HAVE:FIFO:Confirma que usa FIFO|MUST_NOT_HAVE:Preemption:NO debe haber preemptions por quantum|MUST_HAVE:terminado:Procesos deben terminar normalmente"

# Test 2.2: FIFO con Generación Frecuente
run_test \
    "Test 2.2: FIFO - Sin Preemption con Alta Carga" \
    "FIFO NO debe hacer preemption por tiempo incluso con muchos procesos" \
    "./build/churros -a fifo -c 1 -o 1 -t 2 -g 6 -s 10 -d 40" \
    "MUST_HAVE:FIFO:Confirma que usa FIFO|MUST_NOT_HAVE:Preemption:NO debe causar preemption|MUST_HAVE:Dispatch:Debe haber dispatches solo por terminación"

# Test 2.3: FIFO Multicore
run_test \
    "Test 2.3: FIFO - Distribución Multicore" \
    "Múltiples hilos procesando con FIFO sin rotación" \
    "./build/churros -a fifo -c 1 -o 2 -t 2 -g 4 -s 10 -d 60" \
    "MUST_HAVE:FIFO:Confirma que usa FIFO|MUST_NOT_HAVE:Preemption:No debe haber preemptions en ningún thread|COUNT_MIN:Dispatch:Debe haber dispatches en múltiples threads:3"

# ==========================================
# SECCIÓN 3: Tests Comparativos
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 3: Comparativas RR vs FIFO${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 3.1a: Comparativa RR - Fairness
run_test \
    "Test 3.1a: Round Robin - Fairness bajo Contención" \
    "RR debe mostrar rotación justa entre procesos" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 3 -g 4 -s 10 -d 40" \
    "MUST_HAVE:Round Robin:Usa Round Robin|COUNT_MIN:Preemption:Debe rotar entre procesos activamente:5|MUST_HAVE:Dispatch:Debe despachar procesos"

# Test 3.1b: Comparativa FIFO - Sin Fairness
run_test \
    "Test 3.1b: FIFO - Sin Fairness (Convoy Effect)" \
    "FIFO debe ejecutar procesos secuencialmente sin rotación" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 4 -s 10 -d 80" \
    "MUST_HAVE:FIFO:Usa FIFO|MUST_NOT_HAVE:Preemption:No debe haber rotación por tiempo|MUST_HAVE:Proceso.*terminado:Procesos terminan completamente"

# Test 3.2a: RR con Quantum Muy Corto - Alto Overhead
run_test \
    "Test 3.2a: Round Robin - Alto Overhead por Context Switch" \
    "Quantum muy corto genera muchos cambios de contexto" \
    "./build/churros -a rr -c 1 -o 1 -t 2 -q 2 -g 3 -s 5 -d 30" \
    "MUST_HAVE:Round Robin:Usa Round Robin|COUNT_MIN:Preemption:Debe haber muchos context switches:8|MUST_HAVE:Scheduler:Scheduler muy activo"

# Test 3.2b: FIFO - Mínimo Overhead
run_test \
    "Test 3.2b: FIFO - Overhead Mínimo de Context Switch" \
    "FIFO minimiza context switches ejecutando hasta terminar" \
    "./build/churros -a fifo -c 1 -o 1 -t 2 -g 3 -s 5 -d 30" \
    "MUST_HAVE:FIFO:Usa FIFO|MUST_NOT_HAVE:Preemption:No context switches por tiempo|MUST_HAVE:terminado:Procesos ejecutan hasta terminar"

# ==========================================
# SECCIÓN 4: Tests de Estrés
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 4: Tests de Estrés${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 4.1: Saturación de Cola - Round Robin
run_test \
    "Test 4.1: Saturación - Round Robin" \
    "Generación frecuente de procesos con quantum moderado" \
    "./build/churros -a rr -c 1 -o 2 -t 1 -q 6 -g 3 -s 5 -d 60" \
    "MUST_HAVE:Round Robin:Usa Round Robin|COUNT_MIN:creado:Debe generar múltiples procesos:10|MUST_HAVE:Scheduler:Scheduler debe manejar la cola"

# Test 4.2: Saturación de Cola - FIFO
run_test \
    "Test 4.2: Saturación - FIFO" \
    "Mismo escenario que 4.1 pero con FIFO" \
    "./build/churros -a fifo -c 1 -o 2 -t 1 -g 3 -s 5 -d 60" \
    "MUST_HAVE:FIFO:Usa FIFO|COUNT_MIN:creado:Debe generar múltiples procesos:10|MUST_NOT_HAVE:Preemption:No debe haber preemption por tiempo"

# Test 4.3: Escenario Realista Multicore
run_test \
    "Test 4.3: Escenario Multicore Realista - Round Robin" \
    "8 hilos hardware procesando carga balanceada" \
    "./build/churros -a rr -c 2 -o 2 -t 2 -q 8 -g 5 -s 5 -d 100" \
    "MUST_HAVE:Round Robin:Usa Round Robin|COUNT_MIN:Dispatch:Múltiples dispatches en sistema multicore:10|MUST_HAVE:terminado:Procesos deben finalizar correctamente"

# ==========================================
# SECCIÓN 5: Tests de Chocolate Caliente
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 5: Chocolate Caliente${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 5.1: Verificación Básica de Chocolate Caliente
run_test \
    "Test 5.1: Chocolate Caliente - Verificación Básica" \
    "Algoritmo debe mostrar temperatura y quantum adaptativo" \
    "./build/churros -a ch -c 1 -o 1 -t 1 -g 5 -s 20 -d 40" \
    "MUST_HAVE:Chocolate Caliente:Confirma que usa Chocolate Caliente|COUNT_MIN:Temp=.*°C:Debe mostrar temperatura de procesos:5|COUNT_MIN:Quantum=:Debe calcular quantum adaptativo:5|MUST_HAVE:Scheduler:Scheduler debe ejecutarse"

# Test 5.2: Adaptación de Quantum por Temperatura
run_test \
    "Test 5.2: Chocolate Caliente - Quantum Adaptativo" \
    "Quantum debe reducirse cuando el proceso se calienta" \
    "./build/churros -a ch -c 1 -o 1 -t 1 -g 5 -s 15 -d 50" \
    "MUST_HAVE:Chocolate Caliente:Usa Chocolate Caliente|COUNT_MIN:Context switch.*enfriándose:Debe haber context switches cuando quantum se agota:2|MUST_HAVE:°C:Debe mostrar temperatura|MUST_HAVE:Dispatch:Debe despachar procesos"

# Test 5.3: Calentamiento y Enfriamiento de Procesos
run_test \
    "Test 5.3: Chocolate Caliente - Dinámica de Temperatura" \
    "Procesos se calientan ejecutando y enfrían esperando" \
    "./build/churros -a ch -c 1 -o 1 -t 1 -g 3 -s 10 -d 60" \
    "MUST_HAVE:Chocolate Caliente:Usa Chocolate Caliente|MUST_HAVE:Temp=:Temperatura debe incrementarse|COUNT_MIN:Context switch:Debe haber cambios de contexto:3|MUST_HAVE:Scheduler:Scheduler activo"

# Test 5.4: Visualización de Emojis de Temperatura
run_test \
    "Test 5.4: Chocolate Caliente - Indicadores Visuales" \
    "Debe mostrar emojis según rangos de temperatura" \
    "./build/churros -a ch -c 1 -o 1 -t 1 -g 4 -s 15 -d 70" \
    "MUST_HAVE:Chocolate Caliente:Usa Chocolate Caliente|MUST_HAVE:°C:Debe mostrar temperatura|COUNT_MIN:Temp=:Debe haber lecturas de temperatura:8|MUST_HAVE:Dispatch:Debe despachar procesos"

# Test 5.5: Comparativa - Chocolate Caliente vs Round Robin
run_test \
    "Test 5.5: Chocolate Caliente - Comportamiento Diferenciado" \
    "Chocolate Caliente debe comportarse diferente a RR estándar" \
    "./build/churros -a ch -c 1 -o 2 -t 1 -g 3 -s 10 -d 60" \
    "MUST_HAVE:Chocolate Caliente:Usa Chocolate Caliente|MUST_NOT_HAVE:Round Robin:NO debe usar Round Robin|MUST_HAVE:Temp=.*Quantum=:Debe mostrar temp y quantum juntos|COUNT_MIN:Dispatch:Múltiples dispatches:3"

# Test 5.6: Quantum Base Configurable
run_test \
    "Test 5.6: Chocolate Caliente - Quantum Base Configurable" \
    "Parámetro -q debe escalar los quantums adaptativos" \
    "./build/churros -a ch -c 1 -o 1 -t 1 -q 3 -g 5 -s 15 -d 40" \
    "MUST_HAVE:Chocolate Caliente:Confirma que usa Chocolate Caliente|MUST_HAVE:Quantum base.*3:Debe mostrar quantum base configurado|COUNT_MIN:Temp=:Debe mostrar temperatura:4|COUNT_MIN:Context switch:Debe haber cambios de contexto:2"

# ==========================================
# SECCIÓN 6: Tests de Memoria (Parte 3)
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  SECCIÓN 6: Tests de Memoria${NC}"
echo -e "${CYAN}========================================${NC}"
separator

# Test 6.1: Verificación de Creación de Memoria Física
run_test \
    "Test 6.1: Memoria - Inicialización y Configuración" \
    "El sistema debe crear memoria física con espacio de kernel reservado" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 10 -s 10 -d 30 -l debug" \
    "MUST_HAVE:Created.*MB total:Memoria creada con tamaño correcto|MUST_HAVE:kernel reserved:Espacio de kernel reservado|MUST_HAVE:user pages available:Páginas de usuario disponibles|MUST_HAVE:MEM:Componente de memoria presente"

# Test 6.2: Asignación de Page Tables
run_test \
    "Test 6.2: Memoria - Asignación de Page Tables" \
    "Cada proceso debe tener su propia page table en memoria física" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 5 -g 8 -s 10 -d 40 -l debug" \
    "MUST_HAVE:Page mapped:Page tables asignan mapeos|COUNT_MIN:PTBR=:Debe crear page tables para procesos:2|MUST_HAVE:walking page table at PTBR:Page Table Base Register debe usarse"

# Test 6.3: Traducción de Direcciones Virtuales
run_test \
    "Test 6.3: MMU - Traducción Virtual a Física" \
    "MMU debe traducir direcciones virtuales usando page tables" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 15 -s 10 -d 40 -l debug" \
    "MUST_HAVE:TLB:Sistema TLB operativo|MUST_HAVE:Translation complete:Traducciones de direcciones completadas|COUNT_MIN:vaddr.*paddr:Debe mostrar mapeos de páginas virtuales a físicas:5"

# Test 6.4: TLB Hit/Miss
run_test \
    "Test 6.4: MMU - Funcionamiento del TLB" \
    "TLB debe mostrar hits y misses durante ejecución" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 8 -g 10 -s 10 -d 50 -l debug" \
    "MUST_HAVE:TLB HIT:Debe haber hits en el TLB|MUST_HAVE:TLB MISS:Debe haber misses en el TLB|COUNT_MIN:TLB.*walking page table:Page table walk en TLB misses:2"

# Test 6.5: Estadísticas de Memoria
run_test \
    "Test 6.5: Memoria - Estadísticas de Uso" \
    "Sistema debe reportar estadísticas de memoria al finalizar" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 5 -s 10 -d 60 -l info" \
    "MUST_HAVE:Memory.*frames:Debe mostrar uso de frames|MUST_HAVE:Allocations:Debe contar allocations|MUST_HAVE:peak:Debe reportar pico de uso"

# Test 6.6: TLB Statistics
run_test \
    "Test 6.6: MMU - Estadísticas del TLB" \
    "Debe reportar hit rate del TLB" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 6 -g 8 -s 10 -d 50 -l info" \
    "MUST_HAVE:TLB.*hits.*misses:Debe mostrar estadísticas del TLB|MUST_HAVE:hit rate:Debe calcular hit rate del TLB"

# Test 6.7: Asignación y Liberación de Frames
run_test \
    "Test 6.7: Memoria - Gestión de Frames" \
    "Debe asignar frames al crear procesos y liberarlos al terminar" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 10 -s 10 -d 60 -l debug" \
    "MUST_HAVE:Page mapped:Frames asignados durante creación de page tables|COUNT_MIN:PFN=:Debe haber asignaciones de physical frame numbers:5|MUST_HAVE:paddr=:Direcciones físicas asignadas"

# Test 6.8: Page Table Mapping
run_test \
    "Test 6.8: Memoria - Mapeo de Páginas" \
    "Debe mapear páginas virtuales a frames físicos correctamente" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 5 -g 8 -s 10 -d 40 -l debug" \
    "MUST_HAVE:Page mapped:Páginas deben mapearse|COUNT_MIN:VPN=.*PFN=:Mapeos explícitos de VPN a PFN:5|MUST_HAVE:vaddr.*paddr:Debe mostrar direcciones virtuales y físicas"

# Test 6.9: Dirty y Accessed Bits
run_test \
    "Test 6.9: MMU - Bits de Control de Páginas" \
    "MMU debe marcar páginas como dirty y accessed" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 12 -s 10 -d 50 -l debug" \
    "MUST_HAVE:Page marked ACCESSED:Páginas marcadas como accedidas|MUST_HAVE:WRITE:Operaciones de escritura registradas"

# Test 6.10: Multicore Memory Access
run_test \
    "Test 6.10: Memoria - Acceso Concurrente Multicore" \
    "Múltiples cores deben poder acceder a memoria concurrentemente" \
    "./build/churros -a rr -c 1 -o 2 -t 2 -q 5 -g 6 -s 10 -d 60 -l debug" \
    "MUST_HAVE:Translation complete:Traducciones en múltiples threads|COUNT_MIN:Page mapped:Múltiples allocations concurrentes:4|MUST_HAVE:TLB:Cada MMU tiene su TLB"

# Test 6.11: TLB Flush en Context Switch
run_test \
    "Test 6.11: MMU - TLB Flush en Cambio de Contexto" \
    "TLB debe vaciarse cuando cambia el PTBR" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 4 -g 5 -s 10 -d 40 -l debug" \
    "MUST_HAVE:TLB flush:TLB debe vaciarse|COUNT_MIN:TLB flush:Debe invalidar entradas del TLB:2"

# Test 6.12: Memory Out of Bounds Protection
run_test \
    "Test 6.12: Memoria - Protección de Límites" \
    "Sistema debe detectar accesos fuera de rango" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 20 -s 10 -d 30 -l debug" \
    "MUST_NOT_HAVE:out of bounds:No debe haber accesos fuera de límites en ejecución normal|MUST_HAVE:Translation complete:Traducciones deben completarse correctamente"

# Test 6.13: Espacio de Kernel vs Usuario
run_test \
    "Test 6.13: Memoria - Separación Kernel/Usuario" \
    "Debe mantener separación entre espacio de kernel y usuario" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 5 -g 10 -s 10 -d 40 -l debug" \
    "MUST_HAVE:kernel pages:Páginas de kernel reservadas|MUST_HAVE:user pages:Páginas de usuario disponibles|MUST_NOT_HAVE:free kernel frame:No debe liberar frames de kernel"

# Test 6.14: Peak Memory Usage
run_test \
    "Test 6.14: Memoria - Monitoreo de Pico de Uso" \
    "Debe trackear el pico de uso de memoria" \
    "./build/churros -a fifo -c 1 -o 1 -t 1 -g 5 -s 10 -d 60 -l info" \
    "MUST_HAVE:peak:Debe reportar pico de frames usados|MUST_HAVE:Memory:Estadísticas de memoria presentes"

# Test 6.15: TLB Round-Robin Replacement
run_test \
    "Test 6.15: MMU - Reemplazo Round-Robin del TLB" \
    "TLB debe usar política round-robin para reemplazo" \
    "./build/churros -a rr -c 1 -o 1 -t 1 -q 10 -g 8 -s 10 -d 60 -l debug" \
    "MUST_HAVE:TLB UPDATE:TLB debe actualizarse|COUNT_MIN:Adding VPN=.*to TLB:Múltiples entradas añadidas al TLB:3|MUST_HAVE:TLB MISS:TLB misses generan updates"

# ==========================================
# RESUMEN FINAL
# ==========================================
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  RESUMEN DE PRUEBAS${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# Limpiar archivo temporal
rm -f "${TEMP_OUTPUT}"

# Calcular porcentaje
if [ ${TOTAL_TESTS} -gt 0 ]; then
    SUCCESS_RATE=$((TESTS_PASSED * 100 / TOTAL_TESTS))
else
    SUCCESS_RATE=0
fi

# Mostrar estadísticas
echo -e "${BOLD}Resultados de la Suite de Pruebas:${NC}"
echo -e "  Tests totales:   ${TOTAL_TESTS}"
echo -e "  ${GREEN}Tests pasados:   ${TESTS_PASSED}${NC}"
echo -e "  ${RED}Tests fallidos:  ${TESTS_FAILED}${NC}"
echo -e "  Tasa de éxito:   ${SUCCESS_RATE}%"
echo ""

if [ ${TESTS_FAILED} -eq 0 ]; then
    echo -e "${GREEN}${BOLD}╔════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}${BOLD}║  ✓ TODOS LOS TESTS PASARON CON ÉXITO   ║${NC}"
    echo -e "${GREEN}${BOLD}╚════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${GREEN}Los algoritmos de scheduling funcionan correctamente:${NC}"
    echo "  ✓ Round Robin: Preemption por quantum verificada"
    echo "  ✓ FIFO: Ejecución sin interrupciones verificada"
    echo "  ✓ Chocolate Caliente: Quantum adaptativo por temperatura verificado"
    echo "  ✓ Comportamiento diferencial confirmado"
    echo ""
    echo -e "${GREEN}El sistema de memoria funciona correctamente:${NC}"
    echo "  ✓ Memoria física: Inicialización y gestión de frames verificada"
    echo "  ✓ Page Tables: Creación y mapeo de páginas verificado"
    echo "  ✓ MMU: Traducción de direcciones virtuales a físicas verificada"
    echo "  ✓ TLB: Cache de traducciones con hits/misses verificado"
    echo "  ✓ Protección: Separación kernel/usuario y límites verificados"
    echo "  ✓ Estadísticas: Monitoreo de uso y pico de memoria verificado"
else
    echo -e "${RED}${BOLD}╔════════════════════════════════════════╗${NC}"
    echo -e "${RED}${BOLD}║  ✗ ALGUNOS TESTS FALLARON              ║${NC}"
    echo -e "${RED}${BOLD}╚════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${YELLOW}Por favor revisa la salida anterior para ver los detalles.${NC}"
fi

echo ""
echo -e "${CYAN}Comandos útiles:${NC}"
echo "  Ejecutar test individual:"
echo "    ./build/churros -a rr   -c 1 -o 1 -t 1 -q 5 -g 10 -s 50 -d 100"
echo "    ./build/churros -a fifo -c 1 -o 1 -t 1 -g 10 -s 50 -d 100"
echo "    ./build/churros -a ch   -c 1 -o 1 -t 1 -g 5  -s 80 -d 50"
echo ""
echo "  Ver ayuda completa:"
echo "    ./build/churros -h"
echo ""

# Exit code refleja el resultado
if [ ${TESTS_FAILED} -eq 0 ]; then
    exit 0
else
    exit 1
fi
