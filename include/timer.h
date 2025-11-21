#pragma once

#include <cstdint>

namespace sickboy {

    struct Timer {

        Timer();

        void tick();
        double get_time() const;
        std::uint32_t get_fps() const;
        void block_until_next_frame();

    private:

        double frame_start;
        double last_frame;
        double last_fps;
        double delta;
        std::uint32_t frames;
        std::uint32_t fps;

    };

}