#include "timer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace sickboy {

    Timer::Timer() : last_frame(0.0), delta(0.0), frames(0), fps(0) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW.");
        }
        const auto time = get_time();
        frame_start = time;
        last_frame = time;
        last_fps = time;
    }

    void Timer::tick() {
        const auto time = get_time();
        ++frames;
        if ((time - last_fps) >= 1.0) {
            fps = frames;
            frames = 0;
            last_fps = time;
        }
        delta = time - last_frame;
        last_frame = time;
    }

    double Timer::get_time() const {
        return glfwGetTime();
    }

    std::uint32_t Timer::get_fps() const {
        return fps;
    }

    void Timer::block_until_next_frame() {
        static constexpr double target_frametime = 1.0 / 59.73;
        const auto frame_end = frame_start + target_frametime;
        // We cannot rely on sleep as it is widely inaccurate by default, instead we busy wait
        // TODO: This should be optimized further by setting timeBeginPeriod on Windows
        // and sleeping for most of the time difference (target time - 2ms) and then busy waiting
        // for the remaining period.
        while (get_time() < frame_end) {
            _mm_pause();
        }
        frame_start = get_time();
    }

}