#pragma once

#include "mmu.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <memory>
#include <array>
#include <vector>
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
        std::uint16_t period_value;
        std::uint8_t length_timer;
        std::uint8_t duty_cycle_waveform_idx;
        std::uint8_t duty_cycle_sample_idx;

        AudioChannel();

    };

    struct APU {

        APU(const std::shared_ptr<MMU>& memory);
        ~APU();

        bool is_apu_enabled() const;
        void tick();

    private:

        // The number of buffers is a tradeoff between latency and chopiness/stability.
        // Basically we pre-generate audio samples for NUM_BUFFERS * SAMPLES_PER_BUFFER / SAMPLE_RATE duration.
        // So with 4 buffers, 1024 samples per buffer and 48kHz sample rate that would be ~85ms.
        // Assuming a framerate of steady 60Hz that would be (85/16.66) = 5 frames of delay.
        // TODO: Experiment with lowering this and making it configurable. Would be nice to get it down to ~30-50ms.
        static constexpr std::size_t NUM_BUFFERS = 4;
        static constexpr std::size_t NUM_CHANNELS = 4;

        std::shared_ptr<MMU> memory;
        ALCdevice* device;
        ALCcontext* context;
        std::array<ALuint, NUM_BUFFERS> audio_buffers;
        ALuint audio_source;
        std::uint8_t last_div_value;
        std::uint8_t div_apu_counter;
        std::uint8_t buffer_write_index;
        std::uint32_t sample_generation_counter;
        std::array<AudioChannel, NUM_CHANNELS> channels;

        bool is_channel_on(std::uint8_t channel) const;
        void increment_length_timers();
        bool should_generate_buffer_data() const;
        std::vector<std::vector<std::int16_t>> generate_buffer_data(std::uint8_t num_buffers);
        std::uint16_t get_channel_period(std::uint8_t channel) const;
        void initialize_buffers();

    };

}