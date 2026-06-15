#include "timer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <immintrin.h>

#include <stdexcept>

namespace sickboy {

    static constexpr std::uint16_t INTERRUPT_REQUEST_ADDRESS = 0xFF0F;
    static constexpr std::uint16_t TIMER_DIVIDER_ADDRESS = 0xFF04;
    static constexpr std::uint16_t TIMER_COUNTER_ADDRESS = 0xFF05;
    static constexpr std::uint16_t TIMER_MODULO_ADDRESS = 0xFF06;
    static constexpr std::uint16_t TIMER_CONTROL_ADDRESS = 0xFF07;

    Timer::Timer(const std::shared_ptr<MMU>& memory) :
        memory(memory), last_frame(0.0), delta(0.0), frames(0), fps(0), tick_counter(0) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW.");
        }
        const auto time = get_time();
        frame_start = time;
        last_frame = time;
        last_fps = time;
    }

    void Timer::tick_emulator() {
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

    void Timer::tick_system() {
        const auto is_tima_increment_enabled = [this]() -> bool {
            return memory->read(TIMER_CONTROL_ADDRESS) & 0b00000100;
        };
        // The increment mask basically works as a modulo but allows us to use
        // 8-bit values for the operation - for modulo we'd need the value 256
        const auto get_tima_increment_mask = [this]() -> std::uint8_t {
            const std::uint8_t clock_select = memory->read(TIMER_CONTROL_ADDRESS) & 0b00000011;
            switch (clock_select) {
                case 0b00: return 0b11111111; // 256 M-cycles
                case 0b01: return 0b00000011; // 4 M-cycles
                case 0b10: return 0b00001111; // 16 M-cycles
                case 0b11: return 0b00111111; // 64 M-cycles
                default: throw std::runtime_error("Unknown clock select value.");
            }
        };
        // We rely on tick counter overflowing - it is well defined behavior for unsinged ints
        ++tick_counter;
        if (is_tima_increment_enabled()) {
            const auto tima_increment_mask = get_tima_increment_mask();
            if ((tick_counter & tima_increment_mask) == 0) {
                // Check if TIMA is about to overflow
                const auto timer_counter = memory->read(TIMER_COUNTER_ADDRESS);
                if (timer_counter == 0xFF) {
                    // Write timer interrupt request
                    memory->write(INTERRUPT_REQUEST_ADDRESS, memory->read(INTERRUPT_REQUEST_ADDRESS) | (1 << 2));
                    const auto timer_modulo = memory->read(TIMER_MODULO_ADDRESS);
                    memory->write(TIMER_COUNTER_ADDRESS, timer_modulo);
                } else memory->write(TIMER_COUNTER_ADDRESS, timer_counter + 1);
            }
        }

        // Every 64th M-cycle we need to increment the divider
        static constexpr std::uint8_t div_increment_mask = 63;
        if ((tick_counter & div_increment_mask) == 0) {
            const auto divider_value = memory->read(TIMER_DIVIDER_ADDRESS);
            // We need to sidestep DIV resetting to 0 when we write to it, so use direct write
            memory->direct_write(TIMER_DIVIDER_ADDRESS, divider_value + 1);
        }
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

    void Timer::reset_divider_register() {
        memory->write(TIMER_DIVIDER_ADDRESS, 0x00);
    }

}