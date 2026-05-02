#include "apu.h"

#include <stdexcept>

namespace sickboy {

    APU::APU(const std::shared_ptr<MMU>& memory) : memory(memory), device(nullptr), context(nullptr) {
        device = alcOpenDevice(nullptr);
        if (!device) {
            throw std::runtime_error("Failed to open default audio device.");
        }
        context = alcCreateContext(device, nullptr);
        if (!context || !alcMakeContextCurrent(context)) {
            throw std::runtime_error("Failed to create or set audio context.");
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

    }

    bool APU::is_apu_enabled() const {
        static constexpr std::uint16_t AUDIO_MASTER_CONTROL = 0xFF26;
        return memory->read(AUDIO_MASTER_CONTROL) & 0b10000000;
    }

}