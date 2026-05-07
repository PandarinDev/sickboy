#pragma once

#include "mmu.h"

#include <al/alc.h>

#include <memory>
#include <array>
#include <cstdint>

namespace sickboy {

    enum class AudioEnvelope : std::uint8_t {
        DECREASE_VOLUME = 0,
        INCREASE_VOLUME = 1
    };

    struct AudioChannel {

        bool on;
        bool length_enabled;
        std::uint8_t volume;
        AudioEnvelope envelope;
        std::uint8_t sweep_pace;
        std::uint16_t period_counter;

        AudioChannel();

    };

    struct APU {

        APU(const std::shared_ptr<MMU>& memory);
        ~APU();

        bool is_apu_enabled() const;
        void tick();

    private:

        static constexpr std::size_t NUM_CHANNELS = 4;

        std::shared_ptr<MMU> memory;
        ALCdevice* device;
        ALCcontext* context;
        std::array<AudioChannel, NUM_CHANNELS> channels;

        bool is_channel_on(std::uint8_t channel) const;

    };

}