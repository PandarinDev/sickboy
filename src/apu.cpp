#include "apu.h"

#include <format>
#include <stdexcept>

namespace sickboy {

    static constexpr std::uint16_t CHANNEL_1_TIMER_ADDRESS = 0xFF11;
    static constexpr std::uint16_t CHANNEL_1_VOLUME_ADDRESS = 0xFF12;
    static constexpr std::uint16_t CHANNEL_1_CONTROL_ADDRESS = 0xFF14;
    static constexpr std::uint16_t CHANNEL_3_DAC_ENABLED_ADDRESS = 0xFF1A;
    static constexpr std::uint16_t CHANNEL_STRIDE = 0x05;
    static constexpr std::uint16_t TIMER_DIV_ADDRESS = 0xFF04;

    AudioChannel::AudioChannel() :
        on(false), length_enabled(false), volume(0), envelope(AudioEnvelope::DECREASE_VOLUME),
        sweep_pace(0), period_counter(0), length_timer(0) {}

    APU::APU(const std::shared_ptr<MMU>& memory) :
        memory(memory), device(nullptr), context(nullptr),
        last_div_value(memory->read(TIMER_DIV_ADDRESS)), div_apu_counter(0) {
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
                if (is_triggered) {
                    static constexpr std::uint8_t channel_timer_mask = 0b00111111;
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
                    channels[channel].length_timer = channel_timer_register & channel_timer_mask;
                    // TODO: Add duty cycle to the channel from the timer register which controls the output wave form
                }
            });
        }
    }

    APU::~APU() {
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

}