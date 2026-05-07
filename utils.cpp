#include "utils.hpp"
#include <cstdio>
#include <cstdlib>

[[noreturn]] void error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
};
