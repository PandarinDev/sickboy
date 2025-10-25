#include "system.h"
#include "window.h"
#include "timer.h"
#include "renderer.h"

#include <fstream>
#include <iostream>

using namespace sickboy;

int main() {
    try {
        static constexpr auto window_multiplier = 4;
        Window window("SickBoy", 160 * window_multiplier, 144 * window_multiplier);
        System system("assets/tetris.gb");
        Renderer renderer;
        // TODO: Currently we are stuck on the cartridge logo check.
        // The MMU mapping to the cartridge + cartridge loading needs to be implemented.
        bool should_stop = false;
        std::size_t frame_index = 0;
        while (!should_stop) {
            if (system.tick()) {
                window.poll_events();
                renderer.clear_buffers();
                auto frame = system.ppu.compute_frame();
                // TODO: For now we are just writing the frame into PGM images
                {
                    std::ofstream file_handle("frame_" + std::to_string(frame_index) + ".pgm");
                    ++frame_index;
                    // PGM header (type, resolution, max value)
                    file_handle << "P2\n160 144\n255\n";
                    // Pixel data
                    for (std::size_t i = 0; i < frame.size(); ++i) {
                        file_handle << static_cast<int>(frame[i]) << " ";
                    }
                    file_handle << "\n";
                }
                window.swap_buffers();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Encountered exception: " << e.what() << std::endl;
        return 1;
    }
}