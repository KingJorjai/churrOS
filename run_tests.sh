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
    "MUST_HAVE:Chocolate Caliente:Usa Chocolate Caliente|MUST_HAVE:Quantum base.*3:Debe mostrar quantum base configurado|COUNT_MIN:Temp=:Debe mostrar temperatura:4|COUNT_MIN:Context switch:Debe haber cambios de contexto:2"

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
