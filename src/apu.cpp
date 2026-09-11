#include "apu.h"

#include <format>
#include <stdexcept>

#include <string>
#include <iostream>
#include <algorithm>

namespace sickboy {

    static constexpr std::uint16_t CHANNEL_1_TIMER_ADDRESS = 0xFF11;
    static constexpr std::uint16_t CHANNEL_1_VOLUME_ADDRESS = 0xFF12;
    static constexpr std::uint16_t CHANNEL_1_PERIOD_ADDRESS = 0xFF13;
    static constexpr std::uint16_t CHANNEL_1_CONTROL_ADDRESS = 0xFF14;
    static constexpr std::uint16_t CHANNEL_3_DAC_ENABLED_ADDRESS = 0xFF1A;
    static constexpr std::uint16_t CHANNEL_STRIDE = 0x05;
    static constexpr std::uint16_t TIMER_DIV_ADDRESS = 0xFF04;
    static constexpr ALsizei AUDIO_SAMPLE_RATE = 48'000;
    // Buffer sample size should be carefully chosen - the number of buffers multiplied by the samples per buffer must be
    // higher than the max idle time while APU is not getting called. E.g. with 4 buffers, 256 samples per buffer and 48kHz
    // sampling rate our total buffer duration is ~21ms. While waiting for the next frame the max time that can elapse when
    // running in real-time is ~16.6ms. This leaves us with a ~4.5ms extra buffer if all 4 buffers were queued right before
    // the wait, which is very unlikely. Hence 512 samples which is a safe value, but with a bit of extra added latency.
    // Note that we could reduce this if we tried to push buffers more frequently as it can happen that we have data ready
    // in the ringbuffer but we couldn't upload it yet because there were no free buffers at the time.
    static constexpr ALsizei AUDIO_SAMPLES_PER_BUFFER = 512;
    // APU frequency is directly tied to the master clock - note that we are really ticking the APU at 1/4th of this speed
    static constexpr auto APU_FREQUENCY_HZ = 4194304;
    // TODO: This is not entirely accurate - we are losing the fractional part of the divison.
    // We should also come up with a solution for the sample generation counter where the fractional part is respected.
    static constexpr auto APU_TICKS_PER_SAMPLE = APU_FREQUENCY_HZ / 4 / AUDIO_SAMPLE_RATE;
    static constexpr float MASTER_VOLUME = 0.2f;
    static constexpr std::int16_t VOLUME_AMPLITUDE = static_cast<std::int16_t>(32767 * MASTER_VOLUME);

    static std::array<std::uint8_t, 4> PULSE_DUTY_CYCLES = {
        0b00000001, // 12.5%
        0b10000001, // 25.0%
        0b10000111, // 50.0%
        0b01111110, // 75.0%
    };

    AudioChannel::AudioChannel() :
        on(false), length_enabled(false), volume(0), envelope(AudioEnvelope::DECREASE_VOLUME),
        sweep_pace(0), period_value(0), length_timer(0), duty_cycle_waveform_idx(0), duty_cycle_sample_idx(0) {}

    APU::APU(const std::shared_ptr<MMU>& memory) :
        memory(memory), device(nullptr), context(nullptr),
        last_div_value(memory->read(TIMER_DIV_ADDRESS)), div_apu_counter(0),
        sample_generation_counter(0), channels(), ring_buffer(),
        ring_buffer_start_idx(0), ring_buffer_current_idx(0), playback_started(false),
        current_buffer_idx(0) {
        device = alcOpenDevice(nullptr);
        if (!device) {
            throw std::runtime_error("Failed to open default audio device.");
        }
        context = alcCreateContext(device, nullptr);
        if (!context || !alcMakeContextCurrent(context)) {
            throw std::runtime_error("Failed to create or set audio context.");
        }
        for (std::uint8_t channel = 0; channel < NUM_CHANNELS; ++channel) {
            std::uint16_t channel_control_address = CHANNEL_1_CONTROL_ADDRESS + CHANNEL_STRIDE * channel;
            memory->add_write_interceptor(channel_control_address, [this, memory, channel](std::uint16_t address, std::uint8_t value) {
                // Do the actual write
                memory->direct_write(address, value);
                // Adjust channel parameters
                bool is_triggered = (value & 0b10000000) != 0;
                bool length_enabled = (value & 0b01000000) != 0;
                // TODO: What happens if the channel was already enabled/triggered? Do we still re-initialize values?
                if (is_triggered) {
                    static constexpr std::uint8_t timer_mask = 0b00111111;
                    static constexpr std::uint8_t waveform_mask = 0b11000000;
                    static constexpr std::uint8_t volume_mask = 0b11110000;
                    static constexpr std::uint8_t envelope_mask = 0b00001000;
                    static constexpr std::uint8_t sweep_pace_mask = 0b00000111;
                    const std::uint8_t channel_timer_register = memory->read(CHANNEL_1_TIMER_ADDRESS + CHANNEL_STRIDE * channel);
                    const std::uint8_t channel_volume_register = memory->read(CHANNEL_1_VOLUME_ADDRESS + CHANNEL_STRIDE * channel);
                    channels[channel].on = true;
                    channels[channel].length_enabled = length_enabled;
                    channels[channel].volume = (channel_volume_register & volume_mask) >> 4;
                    channels[channel].envelope = static_cast<AudioEnvelope>((channel_volume_register & envelope_mask) >> 3);
                    channels[channel].sweep_pace = channel_volume_register & sweep_pace_mask;
                    channels[channel].length_timer = channel_timer_register & timer_mask;
                    channels[channel].period_value = get_channel_period(channel);
                    channels[channel].duty_cycle_waveform_idx = (channel_timer_register & waveform_mask) >> 6;
                    channels[channel].duty_cycle_sample_idx = 0;
                }
            });
        }

        alGenBuffers(static_cast<ALsizei>(audio_buffers.size()), audio_buffers.data());
        alGenSources(1, &audio_source);
    }

    APU::~APU() {
        alDeleteSources(1, &audio_source);
        alDeleteBuffers(static_cast<ALsizei>(audio_buffers.size()), audio_buffers.data());
        alcMakeContextCurrent(nullptr);
        if (context) {
            alcDestroyContext(context);
        }
        if (device) {
            alcCloseDevice(device);
        }
    }
    
    void APU::tick() {
        static constexpr std::uint8_t DIV_APU_MASK = 0b00010000;
        const auto current_div_value = memory->read(TIMER_DIV_ADDRESS);

        // Tick channel periods and step duty cycle samples - while the APU runs at 1/4th
        // speed compared to the CPU system ensures that APU is ticked for every M-cycle,
        // meaning that we do not need to do any additional checks here.
        for (std::uint8_t i = 0; i < NUM_CHANNELS; ++i) {
            // Max value comes from the fact we have 11 bits for the period => 2^11 = 0x800
            static constexpr std::uint16_t max_value = 0x7FF;
            if (channels[i].period_value == max_value) {
                channels[i].duty_cycle_sample_idx = (channels[i].duty_cycle_sample_idx + 1) % 8;
                channels[i].period_value = get_channel_period(i);
            }
            else {
                channels[i].period_value++;
            }
        }

        // Check if the 4th bit of the timer divider register went 1->0
        if ((last_div_value & DIV_APU_MASK) != 0 && (current_div_value & DIV_APU_MASK) == 0) {
            ++div_apu_counter;
            // Sound length is adjusted every 2nd DIV-APU tick
            if (div_apu_counter % 2 == 0) {
                increment_length_timers();
                // Channel 1 frequency sweep is adjusted every 4th DIV-APU tick
                if (div_apu_counter % 4 == 0) {
                    // TODO: Implement frequency sweep
                    // Envelope sweep is adjusted every 8nd DIV-APU tick
                    if (div_apu_counter % 8 == 0) {
                        // TODO: Implement envelope sweep
                    }
                }
            }
        }
        last_div_value = current_div_value;

        // Check if we need to generate a sample on this tick
        if (should_generate_sample()) {
            const auto sample = generate_sample();
            ring_buffer[ring_buffer_current_idx] = sample;
            ring_buffer_current_idx = (ring_buffer_current_idx + 1) % RING_BUFFER_SAMPLES;
        }
        else {
            ++sample_generation_counter;
        }

        // Check if we have enough data in the ring buffer to upload to audio buffer
        while (has_enough_samples_for_buffer() && upload_samples()) {
            if (playback_started) {
                ALint source_state = 0;
                alGetSourcei(audio_source, AL_SOURCE_STATE, &source_state);
                if (source_state != AL_PLAYING) {
                    alSourcePlay(audio_source);
                }
            }
        }

        // Check for openAL errors
        // TODO: Change this to only occur every X ticks or only if we have made any calls to openAL
        ALenum audio_error = alGetError();
        if (audio_error != AL_NO_ERROR) {
            throw std::runtime_error("OpenAL error received: " + std::to_string((int) audio_error));
        }
    }

    bool APU::is_apu_enabled() const {
        static constexpr std::uint16_t AUDIO_MASTER_CONTROL = 0xFF26;
        return memory->read(AUDIO_MASTER_CONTROL) & 0b10000000;
    }

    bool APU::is_channel_on(std::uint8_t channel) const {
        static constexpr std::uint8_t DAC_MASK = 0xF8; // Upper 5 bits
        if (channel >= NUM_CHANNELS) {
            throw std::runtime_error(std::format("Invalid channel value received '{}'", (int) channel));
        }
        // If APU is disabled all channels are off
        if (!is_apu_enabled()) {
            return false;
        }
        // If a channel's DAC is disabled the channel is off
        // Channel 3 is special as it has its own DAC enabled register
        bool is_dac_enabled = (channel == 2)
            ? ((memory->read(CHANNEL_3_DAC_ENABLED_ADDRESS) & 0b10000000) != 0)
            : ((memory->read(CHANNEL_1_VOLUME_ADDRESS + CHANNEL_STRIDE * channel) & DAC_MASK) != 0);
        if (!is_dac_enabled) {
            return false;
        }
        return channels[channel].on;
    }

    void APU::increment_length_timers() {
        for (std::size_t channel_idx = 0; channel_idx < NUM_CHANNELS; ++channel_idx) {
            auto& channel = channels[channel_idx];
            if (!channel.length_enabled) {
                continue;
            }
            ++channel.length_timer;
            // Channels should turn off at the 64th timer increment, except channel 4 which
            // should turn off at the 256th timer increment (hence the 0, overflow is guaranteed)
            const std::uint8_t timer_threshold = (channel_idx < 3) ? 64 : 0;
            if (channel.length_timer == timer_threshold) {
                channel.on = false;
            }
        }
    }

    bool APU::should_generate_sample() const {
        const auto ring_buffer_next_idx = (ring_buffer_current_idx + 1) % RING_BUFFER_SAMPLES;
        return sample_generation_counter >= APU_TICKS_PER_SAMPLE &&
            ring_buffer_next_idx != ring_buffer_start_idx; // Prevent overwriting existing samples after wrapping around
    }

    std::int16_t APU::generate_sample() {
        // TODO: This only generates for channel#0 now - generate for other channels as well and mix the result
        sample_generation_counter = 0;
        if (!is_channel_on(0)) {
            return 0;
        }
        const std::uint8_t waveform_bits = PULSE_DUTY_CYCLES[channels[0].duty_cycle_waveform_idx];
        const std::uint8_t sample_idx = channels[0].duty_cycle_sample_idx;
        const std::uint8_t waveform_bit = (waveform_bits & (1 << sample_idx)) >> sample_idx;
        return static_cast<std::int16_t>((waveform_bit
            ? (channels[0].volume / 15.0f)
            : -(channels[0].volume / 15.0f)) * VOLUME_AMPLITUDE);
    }

    bool APU::has_enough_samples_for_buffer() const {
        // Trivial case if the ring buffer has not wrapped around
        if (ring_buffer_current_idx >= ring_buffer_start_idx) {
            return (ring_buffer_current_idx - ring_buffer_start_idx) >= AUDIO_SAMPLES_PER_BUFFER;
        }
        // Otherwise calculation is slightly trickier
        return (RING_BUFFER_SAMPLES - ring_buffer_start_idx + ring_buffer_current_idx) >= AUDIO_SAMPLES_PER_BUFFER;
    }

    bool APU::upload_samples() {
        // Check if we have a processed buffer where we can upload data
        if (playback_started) {
            ALint num_processed_buffers = 0;
            alGetSourcei(audio_source, AL_BUFFERS_PROCESSED, &num_processed_buffers);
            if (num_processed_buffers == 0) {
                return false;
            }
        }

        // TODO: Currently we are only ever uploading one buffer worth of data - but maybe we have enough to fill multiple buffers
        ALuint candidate_buffer;
        if (playback_started) {
            alSourceUnqueueBuffers(audio_source, 1, &candidate_buffer);
        }
        else {
            candidate_buffer = audio_buffers[current_buffer_idx++];
        }
        std::vector<std::int16_t> buffer_data(AUDIO_SAMPLES_PER_BUFFER, 0);

        // Trivial case if the ring buffer has not wrapped around
        // TODO: This is jank because we sometimes have data for more than 1 buffer which means current index is all over the place compared to start
        // E.g. maybe current already wrapped around, but the section that we need to upload is sequential at the end.
        if (ring_buffer_current_idx >= ring_buffer_start_idx) {
            memcpy(buffer_data.data(), ring_buffer.data() + ring_buffer_start_idx, AUDIO_SAMPLES_PER_BUFFER * sizeof(std::int16_t));
        }
        // Otherwise we need to copy in two parts
        else {
            std::size_t samples_until_buffer_end = std::min(RING_BUFFER_SAMPLES - ring_buffer_start_idx, (size_t) AUDIO_SAMPLES_PER_BUFFER);
            std::size_t remainder_samples = AUDIO_SAMPLES_PER_BUFFER - samples_until_buffer_end;
            memcpy(buffer_data.data(), ring_buffer.data() + ring_buffer_start_idx, samples_until_buffer_end * sizeof(std::int16_t));
            memcpy(buffer_data.data() + samples_until_buffer_end, ring_buffer.data(), remainder_samples * sizeof(std::int16_t));
        }
        alBufferData(
            candidate_buffer,
            AL_FORMAT_MONO16,
            buffer_data.data(),
            static_cast<ALsizei>(buffer_data.size() * sizeof(std::int16_t)),
            AUDIO_SAMPLE_RATE
        );
        alSourceQueueBuffers(audio_source, 1, &candidate_buffer);
        if (!playback_started && current_buffer_idx == NUM_BUFFERS) {
            alSourcePlay(audio_source);
            playback_started = true;
        }

        // Adjust the start of the ring buffer to the current index
        ring_buffer_start_idx = ring_buffer_current_idx;

        return true;
    }

    std::uint16_t APU::get_channel_period([[maybe_unused]] std::uint8_t channel) const {
        // TODO: This should respect the input channel value
        const std::uint8_t period_lower = memory->read(CHANNEL_1_PERIOD_ADDRESS);
        const std::uint8_t period_higher = memory->read(CHANNEL_1_PERIOD_ADDRESS + 1) & 0b00000111;
        return static_cast<std::uint16_t>((period_higher << 8) | period_lower);
    }

}