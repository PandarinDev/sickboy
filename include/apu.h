#pragma once

#include "mmu.h"

#include <al/alc.h>

#include <memory>

namespace sickboy {

    struct APU {

        APU(const std::shared_ptr<MMU>& memory);
        ~APU();

        bool is_apu_enabled() const;
        void tick();

    private:

        std::shared_ptr<MMU> memory;
        ALCdevice* device;
        ALCcontext* context;

    };

}