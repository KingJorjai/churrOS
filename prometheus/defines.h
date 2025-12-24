#ifndef _defines
#define _defines
/*----------------------------------------------------------------------------- 
 *   defines.h 
 *----------------------------------------------------------------------------*/

#define VIRTUAL_BITS_DEFAULT  24      // bitak
#define PAGE_SIZE_BITS        8
#define USER_LOWEST_ADDRESS   0

#define PROG_NAME_DEFAULT	  "prog"
#define FIRST_NUMBER_DEFAULT  0
#define HOW_MANY_DEFAULT      50
#define MAX_LINE_LENGTH       20    // Kode lerro baten gehienezko tamaina 
#define MAX_LINES_DEFAULT	  20

#define VALUE                 400

typedef struct configuration_t {
    unsigned int  virtual_bits;
    unsigned int  offset_bits;
    unsigned int  offset_mask;
    unsigned int  num_page_bits;
    unsigned int  pages;
    unsigned int  max_lines;
    char		  *prog_name;
    unsigned int  first_number;
    unsigned int  how_many;
} configuration_t;


#endif
