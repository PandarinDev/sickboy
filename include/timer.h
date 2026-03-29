#pragma once

#include "mmu.h"

#include <memory>
#include <cstdint>

namespace sickboy {

    struct Timer {

        Timer(const std::shared_ptr<MMU>& memory);

        void tick_emulator();
        void tick_system();
        double get_time() const;
        std::uint32_t get_fps() const;
        void block_until_next_frame();
        void reset_divider_register();

    private:

        std::shared_ptr<MMU> memory;
        double frame_start;
        double last_frame;
        double last_fps;
        double delta;
        std::uint32_t frames;
        std::uint32_t fps;
        std::uint8_t tick_counter;

    };

}