#!/bin/bash
# Script de pruebas para churrOS

echo "======================================"
echo "  churrOS - Scripts de Prueba"
echo "======================================"
echo ""

# Colores para output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Compilar primero
echo -e "${BLUE}Compilando churrOS...${NC}"
make clean && make
if [ $? -ne 0 ]; then
    echo "Error en la compilación"
    exit 1
fi
echo ""

# Test 1: Round Robin Monohilo (Contención)
# Objetivo: Ver cómo un solo hilo alterna entre procesos.
# Config: 1 CPU, 1 Core, 1 Thread. Quantum=4. Gen=5.
echo -e "${GREEN}Test 1: Round Robin Monohilo (Contención)${NC}"
echo "Descripción: 1 solo hilo hardware debe alternar entre varios procesos."
echo "Parámetros: -c 1 -o 1 -t 1 -p 4 -g 5 -s 50 -d 60"
./build/churros -c 1 -o 1 -t 1 -p 4 -g 5 -s 50 -d 60
echo ""
echo "Presiona Enter para continuar..."
read

# Test 2: Quantum Muy Corto (Alta Interactividad)
# Objetivo: Ver muchos cambios de contexto rápidos.
# Config: 1 CPU, 1 Core, 2 Threads. Quantum=2. Gen=6.
echo -e "${GREEN}Test 2: Quantum Muy Corto (Alta Frecuencia de Switch)${NC}"
echo "Descripción: El quantum es de solo 2 ticks, forzando cambios constantes."
echo "Parámetros: -c 1 -o 1 -t 2 -p 2 -g 6 -s 50 -d 50"
./build/churros -c 1 -o 1 -t 2 -p 2 -g 6 -s 50 -d 50
echo ""
echo "Presiona Enter para continuar..."
read

# Test 3: Saturación de Cola
# Objetivo: Generar procesos más rápido de lo que se pueden terminar.
# Config: 1 CPU, 2 Cores, 1 Thread (2 total). Quantum=5. Gen=3.
echo -e "${GREEN}Test 3: Saturación de Cola${NC}"
echo "Descripción: Se generan procesos (cada 3 ticks) más rápido que el quantum (5 ticks)."
echo "Parámetros: -c 1 -o 2 -t 1 -p 5 -g 3 -s 20 -d 100"
./build/churros -c 1 -o 2 -t 1 -p 5 -g 3 -s 20 -d 100
echo ""
echo "Presiona Enter para continuar..."
read

# Test 4: Escenario Multicore Equilibrado
# Objetivo: Ver distribución de carga en un escenario más realista.
# Config: 2 CPUs, 2 Cores, 2 Threads (8 total). Quantum=10. Gen=5.
echo -e "${GREEN}Test 4: Escenario Multicore Equilibrado${NC}"
echo "Descripción: 8 hilos hardware procesando una carga constante."
echo "Parámetros: -c 2 -o 2 -t 2 -p 10 -g 5 -s 20 -d 100"
./build/churros -c 2 -o 2 -t 2 -p 10 -g 5 -s 20 -d 100
echo ""

echo -e "${YELLOW}¡Pruebas de Round Robin completadas!${NC}"
echo ""
echo "Para ejecutar con parámetros personalizados:"
echo "  ./build/churros -c NUM_CPUS -o NUM_CORES -t NUM_THREADS -p TIMER_PERIOD -g GEN_PERIOD -s SPEED_MS -d DURATION"
echo ""
echo "Para ver ayuda:"
echo "  ./build/churros -h"
