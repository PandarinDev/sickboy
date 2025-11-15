#include "system.h"
#include "window.h"
#include "timer.h"
#include "renderer.h"

#include <iostream>

using namespace sickboy;

int main() {
    try {
        static constexpr auto window_multiplier = 4;
        Window window("SickBoy", PPU::LCD_WIDTH * window_multiplier, PPU::LCD_HEIGHT * window_multiplier);
        System system("assets/blargg/01-special.gb");
        Renderer renderer;
        bool should_stop = false;
        while (!should_stop) {
            if (system.tick()) {
                window.poll_events();
                renderer.clear_buffers();
                renderer.render(system.ppu.frame);
                renderer.check_errors();
                window.swap_buffers();
                should_stop = window.should_close();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Encountered exception: " << e.what() << std::endl;
        return 1;
    }
}