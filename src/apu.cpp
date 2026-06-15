#include "apu.h"

#include <format>
#include <stdexcept>

namespace sickboy {

    static constexpr std::uint16_t CHANNEL_1_TIMER_ADDRESS = 0xFF11;
    static constexpr std::uint16_t CHANNEL_1_VOLUME_ADDRESS = 0xFF12;
    static constexpr std::uint16_t CHANNEL_1_PERIOD_ADDRESS = 0xFF13;
    static constexpr std::uint16_t CHANNEL_1_CONTROL_ADDRESS = 0xFF14;
    static constexpr std::uint16_t CHANNEL_3_DAC_ENABLED_ADDRESS = 0xFF1A;
    static constexpr std::uint16_t CHANNEL_STRIDE = 0x05;
    static constexpr std::uint16_t TIMER_DIV_ADDRESS = 0xFF04;
    static constexpr ALsizei AUDIO_SAMPLE_RATE = 48'000;
    static constexpr ALsizei AUDIO_SAMPLES_PER_BUFFER = 1024;
    static constexpr double AUDIO_BUFFER_DURATION_MS = AUDIO_SAMPLES_PER_BUFFER / (double) AUDIO_SAMPLE_RATE * 1000.0;
    // APU frequency is directly tied to the master clock
    static constexpr auto APU_FREQUENCY_HZ = 4194304;

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
        last_div_value(memory->read(TIMER_DIV_ADDRESS)), div_apu_counter(0), buffer_write_index(0),
        sample_generation_counter(0), channels() {
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
            memory->add_write_interceptor(channel_control_address, [this, memory, channel](std::uint16_t, std::uint8_t value) {
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
        alDeleteBuffers(audio_buffers.size(), audio_buffers.data());
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

        ++sample_generation_counter;
        const auto num_buffer_generation = should_generate_buffer_data();
        if (num_buffer_generation > 0) {
            const auto buffer_data = generate_buffer_data(num_buffer_generation);
            for (std::size_t i = 0; i < buffer_data.size(); ++i) {
                const auto buffer_idx = buffer_write_index;
                buffer_write_index = (buffer_write_index + 1) % NUM_BUFFERS;
                alSourceUnqueueBuffers(audio_source, 1, &audio_buffers[buffer_idx]);
                alBufferData(
                    audio_buffers[buffer_idx],
                    AL_FORMAT_MONO16,
                    buffer_data[i].data(),
                    static_cast<ALsizei>(buffer_data[i].size() * sizeof(std::int16_t)),
                    AUDIO_SAMPLE_RATE
                );
            }
            alSourceQueueBuffers(audio_source, static_cast<ALsizei>(audio_buffers.size()), audio_buffers.data());

            ALint source_state;
            alGetSourcei(audio_source, AL_SOURCE_STATE, &source_state);
            if (source_state != AL_PLAYING) {
                // TODO: Maybe add a warning about detecting audio underrun
                alSourcePlay(audio_source);
            }
            sample_generation_counter = 0;
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

    // Returns how many buffers worth of data we need to generate
    std::uint8_t APU::should_generate_buffer_data() const {
        // We can compute the elapsed emulation time from the number of APU ticks since last sample generation
        const auto elapsed_ms = (sample_generation_counter * 4) / static_cast<double>(APU_FREQUENCY_HZ) * 1000.0;
        return static_cast<std::uint8_t>(elapsed_ms / AUDIO_BUFFER_DURATION_MS);
    }

    std::vector<std::vector<std::int16_t>> APU::generate_buffer_data(std::uint8_t num_buffers) {
        std::vector<std::vector<std::int16_t>> buffer_data;
        buffer_data.resize(num_buffers);
        for (std::size_t buffer_idx = 0; buffer_idx < num_buffers; ++buffer_idx) {
            // Initialize all samples to zero
            std::vector<std::int16_t> samples(AUDIO_SAMPLES_PER_BUFFER, 0);
            // TODO: We are currently only filling samples from channel#1, do this for all channels and mix
            // Period counter determines how often we are dropping samples into the buffer
            const auto waveform_bits = PULSE_DUTY_CYCLES[channels[0].duty_cycle_waveform_idx];
            const auto get_waveform_bit = [waveform_bits](std::uint8_t sample_idx) -> std::uint8_t {
                return (waveform_bits & (1 << sample_idx)) >> sample_idx;
            };
            std::uint8_t current_waveform_bit = get_waveform_bit(channels[0].duty_cycle_sample_idx);
            for (std::size_t sample_idx = 0; sample_idx < AUDIO_SAMPLES_PER_BUFFER; ++sample_idx) {
                // TODO: Implement envelope which would modify the volume field
                // TODO: Rewrite this to cleaner code - volume ranges [0, 15], but 16bit PCM is [-2^16/2, 2^16/2]
                samples[sample_idx] = (current_waveform_bit
                    ? (channels[0].volume / 15.0f)
                    : -(channels[0].volume / 15.0f)) * 32767;
                channels[0].period_value--;
                if (channels[0].period_value == 0) {
                    channels[0].period_value = get_channel_period(0);
                    channels[0].duty_cycle_sample_idx++;
                    current_waveform_bit = get_waveform_bit(channels[0].duty_cycle_sample_idx);
                }
            }
            buffer_data.push_back(std::move(samples));
        }
        return buffer_data;
    }

    std::uint16_t APU::get_channel_period([[maybe_unused]] std::uint8_t channel) const {
        // TODO: This should respect the input channel value
        const std::uint8_t period_lower = memory->read(CHANNEL_1_PERIOD_ADDRESS);
        const std::uint8_t period_higher = memory->read(CHANNEL_1_PERIOD_ADDRESS + 1) & 0b00000111;
        // This could be implemented one of two ways - either cast to signed integer (what the registers
        // actually store) which will be negative and then change the sign to positive, or treat as unsigned
        // and subtract from 2048. Second approach seems more straightforward so doing that here.
        // Also 2048 comes from the fact that we have 11 bits for the period - so this is guaranteed to be non-negative.
        return 2048 - static_cast<std::uint16_t>((period_higher << 8) | period_lower);
    }

}