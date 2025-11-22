#pragma once

#include "mmu.h"

#include <memory>
#include <unordered_set>

namespace sickboy {

    struct InputManager {

        InputManager(const std::shared_ptr<MMU>& memory);
        ~InputManager();

        void tick();

    private:

        struct InputState {
            bool dpad_up;
            bool dpad_down;
            bool dpad_left;
            bool dpad_right;
            bool button_a;
            bool button_b;
            bool button_select;
            bool button_start;
        };

        static std::unordered_set<InputManager*> joystick_event_listeners;

        std::shared_ptr<MMU> memory;
        std::unordered_set<int> connected_joysticks;

        void handle_joystick_event(int jid, int event);
        void detect_joysticks();
        void add_joystick(int jid);
        InputState get_input_state() const;

        static void joystick_event_handler(int jid, int event);

    };

}