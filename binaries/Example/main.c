#include <stdio.h>
#include "prog2.h"
#include <stdlib.h>
#include <unistd.h>

void *(*ll)(size_t, size_t) = &calloc;


int main()
{
    int r = 0;
    void *tmp = &puts;
    int (*fun)(const char *);

    ll(4,4);

    printf("Enter a number: ");
    scanf("%d", &r);

    fun = (int(*)(const char *))tmp;
    fun("Instrumented with PLaTypus!");

    printf("Your number is %d\n", r);
    puts("Printed by normal PLT of puts (outside the masking range).");

    ll = (void *)puts;
    printf("Adderss of fake puts PLT at: 0x%lx\n", (unsigned long) *ll);

    func2(r);

    return 0;
}