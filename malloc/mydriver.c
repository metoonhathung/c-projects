#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mymalloc.h"
#include "mm.h"

int main(int argc, char** argv) {
    myinit(0);
    printf("init\n");
    debug();
    int* arr[11];
    for (int i = 1; i <= 10; i++)
    {   
        printf("malloc %d\n", i);
        arr[i] = mymalloc(i * sizeof(int));
        debug();
    }
    for (int i = 1; i <= 10; i++)
    {
        printf("realloc %d\n", i);
        arr[i] = myrealloc(arr[i], 2 * i * sizeof(int));
        debug();
    }
    for (int i = 1; i <= 10; i++)
    {
        printf("free %d\n", i);
        myfree(arr[i]);
        debug();
    }
    return 0;
}
