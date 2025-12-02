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

# Test 1: Configuración mínima
echo -e "${GREEN}Test 1: Configuración Mínima${NC}"
echo "Parámetros: 1 CPU, 2 cores, 2 HW threads, timer=3, gen=8, velocidad=100ms, duración=30 ticks"
./build/churros -c 1 -o 2 -t 2 -p 3 -g 8 -s 100 -d 30
echo ""
echo "Presiona Enter para continuar..."
read

# Test 2: Configuración media
echo -e "${GREEN}Test 2: Configuración Media${NC}"
echo "Parámetros: 2 CPUs, 4 cores, 2 HW threads, timer=5, gen=10, velocidad=100ms, duración=50 ticks"
./build/churros -c 2 -o 4 -t 2 -p 5 -g 10 -s 100 -d 50
echo ""
echo "Presiona Enter para continuar..."
read

# Test 3: Generación rápida de procesos
echo -e "${GREEN}Test 3: Generación Rápida de Procesos${NC}"
echo "Parámetros: 1 CPU, 2 cores, 2 HW threads, timer=3, gen=5, velocidad=50ms, duración=40 ticks"
./build/churros -c 1 -o 2 -t 2 -p 3 -g 5 -s 50 -d 40
echo ""
echo "Presiona Enter para continuar..."
read

# Test 4: Sistema grande
echo -e "${GREEN}Test 4: Sistema Grande${NC}"
echo "Parámetros: 4 CPUs, 4 cores, 4 HW threads, timer=10, gen=15, velocidad=100ms, duración=100 ticks"
./build/churros -c 4 -o 4 -t 4 -p 10 -g 15 -s 100 -d 100
echo ""
echo "Presiona Enter para continuar..."
read

# Test 5: Simulación rápida
echo -e "${GREEN}Test 5: Simulación Rápida${NC}"
echo "Parámetros: 2 CPUs, 2 cores, 2 HW threads, timer=2, gen=5, velocidad=10ms, duración=100 ticks"
./build/churros -c 2 -o 2 -t 2 -p 2 -g 5 -s 10 -d 100
echo ""

echo -e "${YELLOW}¡Todas las pruebas completadas!${NC}"
echo ""
echo "Para ejecutar con parámetros personalizados:"
echo "  ./build/churros -c NUM_CPUS -o NUM_CORES -t NUM_THREADS -p TIMER_PERIOD -g GEN_PERIOD -s SPEED_MS -d DURATION"
echo ""
echo "Para ver ayuda:"
echo "  ./build/churros -h"
