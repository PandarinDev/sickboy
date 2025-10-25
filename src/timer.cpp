#include "timer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace sickboy {

    double Timer::get_time() const {
        return glfwGetTime();
    }

}