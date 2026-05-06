#include "shrub.hpp"
#include <iostream>

int main() {
    bool ok = check_rice();
    std::cout << "check_rice() returned " << ok << std::endl;
    return 0;
}
