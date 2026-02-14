#pragma once

#include "mmu.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <memory>
#include <unordered_set>

namespace sickboy {

    struct InputManager {

        InputManager(const std::shared_ptr<MMU>& memory, GLFWwindow* window_handle);
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
        static std::unordered_set<InputManager*> key_event_listeners;

        std::shared_ptr<MMU> memory;
        GLFWwindow* window_handle;
        std::unordered_set<int> connected_joysticks;
        std::unordered_set<int> keys_down;

        void handle_joystick_event(int jid, int event);
        void handle_key_event(int key, int action);
        void detect_joysticks();
        void add_joystick(int jid);
        InputState get_input_state() const;

        static void joystick_event_handler(int jid, int event);
        static void key_event_handler(GLFWwindow* window, int key, int scancode, int action, int mods);

    };

}