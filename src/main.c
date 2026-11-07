#include <stdio.h>
#include <string.h>

#define CCARET_VERSION "0.1.0-dev"

static void print_help(void)
{
    puts("C^ (C Caret) Compiler");
    puts("");
    puts("Usage: ccaret <command> [options]");
    puts("");
    puts("Commands:");
    puts("  --help      Show this help");
    puts("  --version   Show compiler version");
}

int main(int argc, char **argv)
{
    if (argc==1 ||
        (argc==2 && strcmp(argv[1], "--help") == 0)) {
       print_help();
       return 0;
    }

    if (argc==2 && strcmp(argv[1], "--version") == 0) {
        printf("ccaret %s\n", CCARET_VERSION);
        return 0;
    }

    fprintf(stderr, "ccaret: unknown command: %s\n", argv[1]);
    return 1;
}
