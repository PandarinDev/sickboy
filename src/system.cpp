#include "system.h"
#include "utils.h"

namespace sickboy {

    System::System() : memory(std::make_shared<MMU>()), cpu(memory), ppu(memory), last_frame(0.0) {
        auto rom_contents = FileUtils::read_binary("assets/dmg_boot.bin");
        if (rom_contents.size() != 256) {
            throw std::runtime_error("Invalid size of boot ROM.");
        }
        memory->copy(0, rom_contents.data(), rom_contents.size());
    }

    void System::tick() {
        // First tick the CPU then catch up the PPU by giving it an equivalent amount of cycles (dots)
        // This is of course not entirely accurate since these subsystems are meant to run asynchronously
        // so in an accurate emulation the PPU might read something from the CPU in-between instructions.
        auto used_cycles = cpu.tick();
        for (std::uint8_t i = 0; i < used_cycles; ++i) {
            ppu.tick();
        }
    }

}