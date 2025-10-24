#include "system.h"

#include <iostream>

using namespace sickboy;

int main() {
    System system("assets/tetris.gb");
    try {
        // TODO: Currently we are stuck on the cartridge logo check.
        // The MMU mapping to the cartridge + cartridge loading needs to be implemented.
        while (true) system.tick();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Encountered exception: " << e.what() << std::endl;
        return 1;
    }
}