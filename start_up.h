#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>

typedef struct{
    char *vanity_prefix;
    int difficulty;
    char *node;
} command_input;


int read_input(command_input *input, int argc, char *argv[]);
