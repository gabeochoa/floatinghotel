#include <cstdio>

int main() {
#ifdef __OPTIMIZE__
    std::puts("optimized");
#else
    std::puts("unoptimized");
#endif
}
