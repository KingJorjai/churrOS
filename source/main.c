/*
 * main.c
 * Punto de entrada del simulador de kernel
 */

#include "../include/kernel.h"
#include "../include/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <unistd.h>
#include <time.h>

static Kernel* main_kernel = NULL;

void print_usage(const char* program_name)
{
    printf("Uso: %s [opciones]\n", program_name);
    printf("Opciones:\n");
    printf("  -c NUM     Número de CPUs (default: 1)\n");
    printf("  -o NUM     Número de cores por CPU (default: 2)\n");
    printf("  -t NUM     Número de HW threads por core (default: 2)\n");
    printf("  -a ALG     Algoritmo de scheduling (default: rr)\n");
    printf("             rr   = Round Robin (con quantum fijo)\n");
    printf("             fifo = FIFO (sin quantum, ejecuta hasta terminar)\n");
    printf("             ch   = Chocolate Caliente (quantum adaptativo)\n");
    printf("  -q NUM     Quantum en ticks (default: 5)\n");
    printf("             - RR: quantum fijo\n");
    printf("             - CH: quantum base para cálculo adaptativo\n");
    printf("  -g NUM     Periodo de generación de procesos en ticks (default: 10)\n");
    printf("  -s NUM     Velocidad del reloj en ms (default: 100)\n");
    printf("  -d NUM     Duración de la simulación en ticks (0=infinito, default: 100)\n");
    printf("  -l LEVEL   Nivel mínimo de log: debug, info, notice, warn, error, critical (default: info)\n");
    printf("  --no-color Deshabilitar colores en logs\n");
    printf("  --no-loc   Ocultar ubicación (cpu:core:thread) en logs\n");
    printf("  -h         Mostrar esta ayuda\n");
    printf("\nEjemplos:\n");
    printf("  %s -a rr -c 2 -o 4 -t 2 -q 5 -g 15 -s 50 -d 200\n", program_name);
    printf("  %s -a fifo -g 20 -d 150 -l debug\n", program_name);
    printf("  %s -a ch -g 8 -s 30 -d 100 --no-color\n", program_name);
}

static int parse_positive_arg(const char* optarg, const char* name)
{
    int value = atoi(optarg);
    if (value <= 0) {
        fprintf(stderr, "Error: %s debe ser mayor que 0\n", name);
        exit(1);
    }
    return value;
}

int main(int argc, char* argv[])
{
    srand(time(NULL));
    
    // Inicializar sistema de logging con valores por defecto
    log_set_level(LOG_LEVEL_INFO);
    log_set_colors_enabled(1);
    log_set_location_enabled(1);
    
    KernelConfig config = {
        .num_cpus = 1,
        .num_cores_per_cpu = 2,
        .num_hw_threads_per_core = 2,
        .scheduler_algorithm = SCHEDULER_ROUND_ROBIN,
        .rr_quantum = 5,
        .process_gen_period = 10,
        .clock_speed_ms = 100,
        .simulation_duration = 100
    };
    
    int opt;
    
    // Primero procesamos opciones largas manualmente
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-color") == 0) {
            log_set_colors_enabled(0);
        } else if (strcmp(argv[i], "--no-loc") == 0) {
            log_set_location_enabled(0);
        }
    }
    
    while ((opt = getopt(argc, argv, "c:o:t:a:q:p:g:s:d:l:h")) != -1) {
        switch (opt) {
            case 'c': config.num_cpus = parse_positive_arg(optarg, "El número de CPUs"); break;
            case 'o': config.num_cores_per_cpu = parse_positive_arg(optarg, "El número de cores"); break;
            case 't': config.num_hw_threads_per_core = parse_positive_arg(optarg, "El número de HW threads"); break;
            case 'a':
                if (strcmp(optarg, "rr") == 0) {
                    config.scheduler_algorithm = SCHEDULER_ROUND_ROBIN;
                } else if (strcmp(optarg, "fifo") == 0) {
                    config.scheduler_algorithm = SCHEDULER_FIFO;
                } else if (strcmp(optarg, "ch") == 0) {
                    config.scheduler_algorithm = SCHEDULER_CHOCOLATE_CALIENTE;
                } else {
                    fprintf(stderr, "Error: Algoritmo '%s' no reconocido. Use 'rr', 'fifo' o 'ch'\n", optarg);
                    return 1;
                }
                break;
            case 'q': config.rr_quantum = parse_positive_arg(optarg, "El quantum"); break;
            case 'p': config.rr_quantum = parse_positive_arg(optarg, "El quantum"); break; /* Compatibilidad con -p */
            case 'g': config.process_gen_period = parse_positive_arg(optarg, "El periodo de generación"); break;
            case 's': config.clock_speed_ms = parse_positive_arg(optarg, "La velocidad del reloj"); break;
            case 'd': config.simulation_duration = atoi(optarg); break;
            case 'l':
                if (strcmp(optarg, "debug") == 0) {
                    log_set_level(LOG_LEVEL_DEBUG);
                } else if (strcmp(optarg, "info") == 0) {
                    log_set_level(LOG_LEVEL_INFO);
                } else if (strcmp(optarg, "notice") == 0) {
                    log_set_level(LOG_LEVEL_NOTICE);
                } else if (strcmp(optarg, "warn") == 0) {
                    log_set_level(LOG_LEVEL_WARNING);
                } else if (strcmp(optarg, "error") == 0) {
                    log_set_level(LOG_LEVEL_ERROR);
                } else if (strcmp(optarg, "critical") == 0) {
                    log_set_level(LOG_LEVEL_CRITICAL);
                } else {
                    fprintf(stderr, "Error: Nivel de log '%s' no reconocido\n", optarg);
                    fprintf(stderr, "Use: debug, info, notice, warn, error, critical\n");
                    return 1;
                }
                break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    main_kernel = kernel_create(&config);
    if (!main_kernel) {
        log_message(LOG_LEVEL_CRITICAL, LOG_COMPONENT_KERNEL, "No se pudo crear el kernel");
        return 1;
    }
    
    log_message(LOG_LEVEL_INFO, LOG_COMPONENT_KERNEL, "Iniciando simulador de kernel churrOS");
    
    if (kernel_start(main_kernel) != 0) {
        log_message(LOG_LEVEL_CRITICAL, LOG_COMPONENT_KERNEL, "No se pudo iniciar el kernel");
        kernel_destroy(main_kernel);
        return 1;
    }
    
    pthread_join(main_kernel->clock_thread, NULL);
    
    LOG_INFO(LOG_COMPONENT_KERNEL, "Estadísticas Finales");
    if (log_get_level() <= LOG_LEVEL_INFO) {
        char line[256];
        snprintf(line, sizeof(line), "          │ Procesos totales generados: %u\n", main_kernel->next_pid - 1);
        write(STDOUT_FILENO, line, strlen(line));
        snprintf(line, sizeof(line), "          │ Procesos en cola: %u\n", process_queue_size(main_kernel->process_queue));
        write(STDOUT_FILENO, line, strlen(line));
    }
    machine_print_status(main_kernel->machine);
    kernel_destroy(main_kernel);
    
    log_message(LOG_LEVEL_INFO, LOG_COMPONENT_KERNEL, "Simulador terminado correctamente");
    return 0;
}
