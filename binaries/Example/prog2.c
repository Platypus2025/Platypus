#include "prog2.h"
#include <stdlib.h>

typedef void (*CallbackFunction)(int);
typedef int (*CallbackFunction2)(const char *);

void func2(int b) {
    func3(puts);
    b += 1;
}

void func3(CallbackFunction2 callback) {
    callback("Printed by a callback!");
}

void * func1() {
    return (void *)malloc;
}