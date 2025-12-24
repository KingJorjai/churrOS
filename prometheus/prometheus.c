/*═════════════════════════════════════════════════════════════════════════════ 
 *   prometheus.c 
 *
 *      ./prometheus -s 0 -nprog -f0  -l20   -p60
        ./prometheus -s 3 -nprog -f60 -l1000 -p1
        ./prometheus -s 9 -nprog -f61 -l20   -p60
 *
 *════════════════════════════════════════════════════════════════════════════*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <string.h>

#include "defines.h"

struct configuration_t conf;

unsigned int     user_lowest;
unsigned int     user_highest;
unsigned int     user_space;

void __konfigurazioa(int argc, char *argv[]);
void __error(int cod, char *s);
void __message(int cod);

int main(int argc, char *argv[]) {
    FILE             *fd;
    char             line[MAX_LINE_LENGTH], file_name[MAX_LINE_LENGTH];
    unsigned int     i, pnum, datum;
    unsigned int     code_start, code_size;
    unsigned int     data_start, data_size;
    unsigned int     var_offset;
    unsigned char    reg1, reg2, reg3;

    __konfigurazioa(argc, argv);   // Konfigurazioa

    user_lowest   = USER_LOWEST_ADDRESS;
    user_highest  = (1 << conf.virtual_bits) - 1;
    user_space    = user_highest - user_lowest + 1;

    __message(0);

    for(pnum = conf.first_number; pnum < (conf.first_number + conf.how_many); pnum++){

        sprintf(file_name,"%s%03d.elf", conf.prog_name, pnum);
        if((fd = fopen(file_name, "w")) == NULL){
            __error(0,"Error while opening file");
        } // if

        code_start = user_lowest & ~conf.offset_mask;
        code_size = 4 + (rand() % conf.max_lines); // Gutxienez, agindu multzo bat 
        data_start = (code_start + ((code_size >> 2) << 2) + 1) << 2;
        data_size = 4 + (rand() % conf.max_lines);

        sprintf(line,".text %06X\n", code_start);
        fputs(line, fd);
        sprintf(line,".data %06X\n", data_start);
        fputs(line, fd);

        for (i=0; i < (code_size >> 2); i++) { //  lau lerrotako blokeak: ld ld add st
            reg1 = rand() % 16;
            var_offset  = (rand() % data_size) << 2;
            sprintf(line,"0%1X%06X\n", reg1, data_start + var_offset); //ld
            fputs(line, fd);

            reg2 = (reg1 + 1) % 16;
            var_offset  = (((var_offset >> 2) + 1) % data_size) << 2;
            sprintf(line,"0%1X%06X\n", reg2, data_start + var_offset); // ld
            fputs(line, fd);

            reg3 = (reg1 + 2) % 16;
            sprintf(line,"2%1X%1X%1X0000\n", reg3, reg1, reg2); // add
            fputs(line, fd);

            var_offset  = (((var_offset >> 2) + 1) % data_size) << 2;
            sprintf(line,"1%1X%06X\n", reg3, data_start + var_offset); // st
            fputs(line, fd);
        } // for code

        sprintf(line,"F0000000\n"); // exit
        fputs(line, fd);

        for (i=0; i < data_size; i++) {
            datum = (rand() % VALUE) - (VALUE >> 1);
            sprintf(line,"%08X\n", datum);
            fputs(line, fd);
         } // for data

         fclose(fd);
    }

    return 0;

} // main

void __konfigurazioa(int argc, char *argv[]){

    int opt, long_index;
    int seed = 0;
    static struct option long_options[] = {
        {"first",      required_argument, 0,  'f' },
        {"help",       no_argument,       0,  'h' },
        {"lines",      required_argument, 0,  'l' },
        {"name",       required_argument, 0,  'n' },
        {"programs",   required_argument, 0,  'p' },
        {"seed",       required_argument, 0,  's' },
        {0,            0,                 0,   0  }
    };

    conf.virtual_bits = VIRTUAL_BITS_DEFAULT;
    conf.offset_bits  = PAGE_SIZE_BITS;
    conf.max_lines    = MAX_LINES_DEFAULT;
    conf.prog_name    = PROG_NAME_DEFAULT;
    conf.first_number = FIRST_NUMBER_DEFAULT;
    conf.how_many = HOW_MANY_DEFAULT;

    long_index =0;
    while ((opt = getopt_long(argc, argv,":f:hl:n:p:s:", 
                        long_options, &long_index )) != -1) {
      switch(opt) {
        case 'f':   /* -f or --first */ 
            conf.first_number = atoi(optarg);
            break; 
        case 'h':   /* -h or --help */
        case '?':
            printf ("Uso: %s [OPTIONS]\n", argv[0]);
            printf ("  -f  --first=NNN\t"
                "Primer número del nombre [%d]\n", FIRST_NUMBER_DEFAULT);
            printf ("  -h, --help\t\t"
               "Ayuda\n");
            printf ("  -l  --lines=NNN\t"
                "Número de líneas (aproximado) [%d]\n", MAX_LINES_DEFAULT);
            printf ("  -n  --name=SSS\t"
               "Inicial del nombre del programa [%s]\n", PROG_NAME_DEFAULT);
            printf ("  -p  --programs=NNN\t"
                "Número máximo de programas [%d]\n", HOW_MANY_DEFAULT);
            printf ("  -s  --seed=N\t"
                "Semilla para crear numeros aleatorios [%d]\n", seed);

            printf ("Ejemplos:\n");
            printf ("  ./prometheus -s 0 -nprog -f0  -l20   -p60\n");
            printf ("  ./prometheus -s 3 -nprog -f60 -l1000 -p1\n");
            printf ("  ./prometheus -s 9 -nprog -f61 -l20   -p60\n");
            exit(0);
        case 'l':   /* -l or --lines */ 
            conf.max_lines = atoi(optarg);
            break;
        case 'n':   /* -n or --name */ 
            conf.prog_name = optarg;
            break; 
        case 'p':   /* -p or --programs */ 
            conf.how_many = atoi(optarg);
            break;
        case 's':
            seed = atoi(optarg);
            break; 
        default:
            __error(0, "Unknown argument option"); 
      } 
    } 

    conf.offset_mask    = (1 << conf.offset_bits) - 1;
    conf.num_page_bits  = conf.virtual_bits - conf.offset_bits;
    conf.pages          = 1 << conf.num_page_bits;

    srand (seed);
} 

/*----------------------------------------------------------------------------- 
 *   Mezuak
 *----------------------------------------------------------------------------*/

void __message(int cod) {
  time_t rawtime;
  struct tm *ptm;
  
  switch (cod) {
    case 0: 
            rawtime = time(NULL);
            ptm = localtime(&rawtime);
	    printf("╔═══════════════════════════════════╗\n");
            printf("║  SE·SO          %02d/%02d/%04d  %02d:%02d ║\n",
                                                    ptm->tm_mday, 
                                                    ptm->tm_mon+1, 
                                                    ptm->tm_year+1900, 
                                                    ptm->tm_hour, 
                                                    ptm->tm_min);
            printf("║        ☼☼☼ Prometheus ☼☼☼         ║\n");
            printf("║ Nombre de los programas: %-2s     ║\n", conf.prog_name);
            printf("║   ├ Primer número: %03d            ║\n", conf.first_number);
            printf("║   ├ Número de programas: %-3d      ║\n", conf.how_many);            
            printf("║ Page/Frame size: 2^%-3d %-8d   ║\n", conf.offset_bits, (1<<conf.offset_bits));
            printf("║ Memoria Virtual: 2^%-3d            ║\n", conf.virtual_bits);
            printf("║   ├ pages: %-16d       ║\n", conf.pages);
            printf("║ User lowest:    0x%06X          ║\n", user_lowest);
            printf("║    ├ highest:   0x%06X          ║\n", user_highest);
            printf("╚═══════════════════════════════════╝\n\n");
           
            break; // 0
    default: 
            break; // default
  } // switch
}

void __error(int cod, char *s) {
  switch (cod) {
    case 0: 
            printf("☼☼☼☼☼☼☼☼ %s ☼☼☼☼☼☼☼☼\n", s);
            break;
    default: 
            break;
  }
  exit(0);
}
