/*
 * main.c
 * Punto de entrada del simulador de kernel
 */

#include "../include/kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("             rr   = Round Robin\n");
    printf("             fifo = FIFO\n");
    printf("             ch   = Chocolate Caliente (quantum adaptativo)\n");
    printf("  -p NUM     Periodo del timer en ticks (default: 5)\n");
    printf("  -g NUM     Periodo de generación de procesos en ticks (default: 10)\n");
    printf("  -s NUM     Velocidad del reloj en ms (default: 100)\n");
    printf("  -d NUM     Duración de la simulación en ticks (0=infinito, default: 100)\n");
    printf("  -h         Mostrar esta ayuda\n");
    printf("\nEjemplos:\n");
    printf("  %s -c 2 -o 4 -t 2 -p 5 -g 15 -s 50 -d 200\n", program_name);
    printf("  %s -a fifo -p 10 -g 20 -d 150\n", program_name);
    printf("  %s -a ch -p 3 -g 8 -s 30 -d 100  # ¡Chocolate Caliente!\n", program_name);
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
    
    KernelConfig config = {
        .num_cpus = 1,
        .num_cores_per_cpu = 2,
        .num_hw_threads_per_core = 2,
        .scheduler_algorithm = SCHEDULER_ROUND_ROBIN,
        .timer_period = 5,
        .process_gen_period = 10,
        .clock_speed_ms = 100,
        .simulation_duration = 100
    };
    
    int opt;
    while ((opt = getopt(argc, argv, "c:o:t:a:p:g:s:d:h")) != -1) {
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
            case 'p': config.timer_period = parse_positive_arg(optarg, "El periodo del timer"); break;
            case 'g': config.process_gen_period = parse_positive_arg(optarg, "El periodo de generación"); break;
            case 's': config.clock_speed_ms = parse_positive_arg(optarg, "La velocidad del reloj"); break;
            case 'd': config.simulation_duration = atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    main_kernel = kernel_create(&config);
    if (!main_kernel) {
        fprintf(stderr, "Error: No se pudo crear el kernel\n");
        return 1;
    }
    
    if (kernel_start(main_kernel) != 0) {
        fprintf(stderr, "Error: No se pudo iniciar el kernel\n");
        kernel_destroy(main_kernel);
        return 1;
    }
    
    pthread_join(main_kernel->clock_thread, NULL);
    
    printf("\n=== Estadísticas Finales ===\n");
    printf("Procesos totales generados: %u\n", main_kernel->next_pid - 1);
    printf("Procesos en cola: %u\n", process_queue_size(main_kernel->process_queue));
    printf("\n");
    
    machine_print_status(main_kernel->machine);
    kernel_destroy(main_kernel);
    
    printf("\nSimulador terminado correctamente.\n");
    return 0;
}
