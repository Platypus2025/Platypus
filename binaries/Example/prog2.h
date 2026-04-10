#include <stdio.h>

typedef void (*CallbackFunction)(int);
typedef int (*CallbackFunction2)(const char *);


void func2(int b);
void func3(CallbackFunction2 callback);
void * func1();