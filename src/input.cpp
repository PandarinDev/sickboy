#include "input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <format>
#include <iostream>
#include <stdexcept>
#include <functional>

#include <iostream>

namespace sickboy {

    std::unordered_set<InputManager*> InputManager::joystick_event_listeners;
    std::unordered_set<InputManager*> InputManager::key_event_listeners;
        
    InputManager::InputManager(const std::shared_ptr<MMU>& memory, GLFWwindow* window_handle) :
        memory(memory), window_handle(window_handle) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW.");
        }
        joystick_event_listeners.emplace(this);
        key_event_listeners.emplace(this);
        // Set joystick callback and detect already connected joysticks
        glfwSetJoystickCallback(joystick_event_handler);
        detect_joysticks();
        // Set key event listener
        glfwSetKeyCallback(window_handle, key_event_handler);
    }

    InputManager::~InputManager() {
        joystick_event_listeners.erase(this);
        key_event_listeners.erase(this);
    }

    void InputManager::tick() {
        // Write input state into MMU registers
        static constexpr std::uint16_t JOYPAD_INPUT_ADDRESS = 0xFF00;
        const std::uint8_t joypad_value = memory->read(JOYPAD_INPUT_ADDRESS);
        const bool select_buttons = (joypad_value & 0b00100000) == 0;
        const bool select_dpad = (joypad_value & 0b00010000) == 0;
        const std::uint8_t joypad_high_nibble = joypad_value & 0xF0;
        // Start off values as "all buttons released" (lower nibble all 1s)
        std::uint8_t values = joypad_high_nibble | 0x0F;
        bool need_interrupt = false;
        const auto set_value_bit = [&joypad_value, &values, &need_interrupt](bool input, std::uint8_t bit) {
            std::uint8_t bitmask = (1 << bit);
            // Do nothing if the button is released
            if (!input) {
                return;
            }
            // Clear the bit corresponding to the button
            values &= ~bitmask;
            // If the button wasn't pressed previously require a joypad interrupt
            if ((joypad_value & bitmask) != 0) {
            need_interrupt = true;
            }
        };
        
        // Only query the input state if either buttons or dpad is selected as polling inputs is costly
        if (select_buttons || select_dpad) {
            const auto input_state = get_input_state();
            if (select_buttons) {
                set_value_bit(input_state.button_start, 3);
                set_value_bit(input_state.button_select, 2);
                set_value_bit(input_state.button_a, 1);
                set_value_bit(input_state.button_b, 0);
            }
            if (select_dpad) {
                set_value_bit(input_state.dpad_down, 3);
                set_value_bit(input_state.dpad_up, 2);
                set_value_bit(input_state.dpad_left, 1);
                set_value_bit(input_state.dpad_right, 0);
            }
        }
        // Write the new value and trigger joystick interrupt if any of the lower nibble bits went 1->0 (pressed)
        // Important to use direct write as regular write protects against changes in the lower nibble
        memory->direct_write(JOYPAD_INPUT_ADDRESS, values);

        if (need_interrupt) {
            static constexpr std::uint16_t INTERRUPT_REQUEST_ADDRESS = 0xFF0F;
            memory->write(INTERRUPT_REQUEST_ADDRESS, memory->read(INTERRUPT_REQUEST_ADDRESS) | (1 << 4));
        }
    }

    void InputManager::handle_joystick_event(int jid, int event) {
        if (event == GLFW_CONNECTED) {
            add_joystick(jid);
        }
        else if (event == GLFW_DISCONNECTED) {
            connected_joysticks.erase(jid);
        }
    }

    void InputManager::handle_key_event(int key, int action) {
        if (action != GLFW_RELEASE) {
            keys_down.emplace(key);
        }
        else {
            keys_down.erase(key);
        }
    }

    void InputManager::detect_joysticks() {
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (glfwJoystickPresent(jid)) {
                add_joystick(jid);
            }
        }
    }

    void InputManager::add_joystick(int jid) {
        if (glfwJoystickIsGamepad(jid)) {
            connected_joysticks.emplace(jid);
        }
        else {
            std::cerr << "Connected joystick '" << glfwGetJoystickName(jid) << "' is not supported, ignoring it." << std::endl;
        }
    }

    InputManager::InputState InputManager::get_input_state() const {
        // Ensure that we polled inputs before constructing input state
        glfwPollEvents();

        // Construct input state
        InputState input_state{};
        for (const auto jid : connected_joysticks) {
            GLFWgamepadstate gamepad_state{};
            if (glfwGetGamepadState(jid, &gamepad_state)) {
                input_state.dpad_up |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS);
                input_state.dpad_down |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS);
                input_state.dpad_left |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS);
                input_state.dpad_right |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS);
                // Due to different layout between DMG and typical controller layout A/B is swapped
                input_state.button_a |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS);
                input_state.button_b |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS);
                input_state.button_select |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_GUIDE] == GLFW_PRESS);
                input_state.button_start |= (gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_START] == GLFW_PRESS);
            }
        }

        // Also use keyboard state
        const auto is_key_down = [this](int key) {
            return keys_down.find(key) != keys_down.cend();
        };
        input_state.dpad_up |= is_key_down(GLFW_KEY_UP);
        input_state.dpad_down |= is_key_down(GLFW_KEY_DOWN);
        input_state.dpad_left |= is_key_down(GLFW_KEY_LEFT);
        input_state.dpad_right |= is_key_down(GLFW_KEY_RIGHT);
        input_state.button_a |= is_key_down(GLFW_KEY_A);
        input_state.button_b |= is_key_down(GLFW_KEY_B);
        input_state.button_select |= is_key_down(GLFW_KEY_BACKSPACE);
        input_state.button_start |= is_key_down(GLFW_KEY_ENTER);

        return input_state;
    }

    void InputManager::joystick_event_handler(int jid, int event) {
        for (auto& instance : joystick_event_listeners) {
            instance->handle_joystick_event(jid, event);
        }
    }

    void InputManager::key_event_handler(GLFWwindow*, int key, int, int action, int) {
        for (auto& instance : key_event_listeners) {
            instance->handle_key_event(key, action);
        }
    }

}