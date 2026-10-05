#include <stdio.h>
#include <stdlib.h>
#include "../include/parser.h"
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    unsigned char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    printf("%d\n", filter(buf, buf + n, 8080));
    return 0;
}
