#include <ldsodefs.h>

char placeholder_symbols[10][48] __attribute__((section(".got"))) = {""};
void *placeholder_plts[10] __attribute__((section(".got"))) = {NULL};
int placeholded = 0;
