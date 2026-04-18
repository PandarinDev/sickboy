#include "system.h"
#include "window.h"
#include "input.h"
#include "renderer.h"

#include <iostream>

using namespace sickboy;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Invalid usage: sickboy <game_path>" << std::endl;
        return 1;
    }
    try {
        static constexpr auto window_multiplier = 4;
        Window window("SickBoy", PPU::LCD_WIDTH * window_multiplier, PPU::LCD_HEIGHT * window_multiplier);
        System system("assets/bootix_dmg.bin", argv[1]);
        Renderer renderer;
        InputManager input_manager(system.memory, window.get_handle());
        bool should_stop = false;
        while (!should_stop) {
            // Inputs need to be ticked every frame as inputs can be queried at any state
            input_manager.tick();
            if (system.tick()) {
                window.poll_events();
                renderer.clear_buffers();
                renderer.render(system.ppu.frame);
                renderer.check_errors();
                window.swap_buffers();
                should_stop = window.should_close();
                system.timer.block_until_next_frame();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Encountered exception: " << e.what() << std::endl;
        return 1;
    }
}