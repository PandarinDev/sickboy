#include "system.h"
#include "window.h"
#include "input.h"
#include "renderer.h"
#include "utils.h"

#include <rapidjson/document.h>

#include <iostream>
#include <filesystem>

using namespace sickboy;

int main() {
    for (const auto& test_file : std::filesystem::directory_iterator("assets/cpu_test")) {
        const auto file_path = test_file.path();
        if (!test_file.is_regular_file() || file_path.extension() != ".json") {
            std::cout << std::format("Skipping '{}' due to unsupported extension.", file_path.string()) << std::endl;
            continue;
        }
        std::cout << std::format("Executing tests inside '{}'...", file_path.string()) << std::endl;
        try {
            const auto input_str = FileUtils::read_string(file_path);
            rapidjson::Document input_doc;
            input_doc.Parse(input_str.c_str());
            const auto root_array = input_doc.GetArray();
            for (const auto& test_case : root_array) {
                const auto& test_obj = test_case.GetObject();
                const auto& test_name = test_obj["name"].GetString();
                const auto& initial_state_obj = test_obj["initial"].GetObject();

                // Initialize memory
                const auto memory = std::make_shared<MMU>();
                memory->set_boot_rom_enabled(false);

                // Initialize CPU
                CPU cpu(memory);
                cpu.registers.a() = static_cast<std::uint8_t>(initial_state_obj["a"].GetInt());
                cpu.registers.b() = static_cast<std::uint8_t>(initial_state_obj["b"].GetInt());
                cpu.registers.c() = static_cast<std::uint8_t>(initial_state_obj["c"].GetInt());
                cpu.registers.d() = static_cast<std::uint8_t>(initial_state_obj["d"].GetInt());
                cpu.registers.e() = static_cast<std::uint8_t>(initial_state_obj["e"].GetInt());
                cpu.registers.f() = static_cast<std::uint8_t>(initial_state_obj["f"].GetInt());
                cpu.registers.h() = static_cast<std::uint8_t>(initial_state_obj["h"].GetInt());
                cpu.registers.l() = static_cast<std::uint8_t>(initial_state_obj["l"].GetInt());
                cpu.registers.pc = static_cast<std::uint16_t>(initial_state_obj["pc"].GetInt()) - 1;
                cpu.registers.sp = static_cast<std::uint16_t>(initial_state_obj["sp"].GetInt());

                // Initialize values in memory
                const auto& ram_values = initial_state_obj["ram"].GetArray();
                for (const auto& ram_value : ram_values) {
                    const auto& ram_value_array = ram_value.GetArray();
                    const auto address = static_cast<std::uint16_t>(ram_value_array[0].GetInt());
                    const auto value = static_cast<std::uint8_t>(ram_value_array[1].GetInt());
                    memory->write(address, value);
                }

                // Execute CPU cycle
                bool additional_tick = false;
                if (memory->read(cpu.registers.pc) == 0xCB) {
                    additional_tick = true;
                }
                cpu.tick();
                if (additional_tick) cpu.tick();

                // Assert on the results
                const auto& final_state_obj = test_obj["final"].GetObject();
                #define ASSERT_EQUALS(reg, expected, actual) if (expected != actual) throw std::runtime_error(std::format("Test '{}' for register '{}' failed, expected {}, found {}", test_name, reg, expected, actual));
                ASSERT_EQUALS("a", static_cast<std::uint8_t>(final_state_obj["a"].GetInt()), cpu.registers.a())
                ASSERT_EQUALS("b", static_cast<std::uint8_t>(final_state_obj["b"].GetInt()), cpu.registers.b())
                ASSERT_EQUALS("c", static_cast<std::uint8_t>(final_state_obj["c"].GetInt()), cpu.registers.c())
                ASSERT_EQUALS("d", static_cast<std::uint8_t>(final_state_obj["d"].GetInt()), cpu.registers.d())
                ASSERT_EQUALS("e", static_cast<std::uint8_t>(final_state_obj["e"].GetInt()), cpu.registers.e())
                ASSERT_EQUALS("f", static_cast<std::uint8_t>(final_state_obj["f"].GetInt()), cpu.registers.f())
                ASSERT_EQUALS("h", static_cast<std::uint8_t>(final_state_obj["h"].GetInt()), cpu.registers.h())
                ASSERT_EQUALS("l", static_cast<std::uint8_t>(final_state_obj["l"].GetInt()), cpu.registers.l())
                ASSERT_EQUALS("pc", static_cast<std::uint16_t>(final_state_obj["pc"].GetInt()) - 1, cpu.registers.pc)
                ASSERT_EQUALS("sp", static_cast<std::uint16_t>(final_state_obj["sp"].GetInt()), cpu.registers.sp)

                const auto& final_ram_values = final_state_obj["ram"].GetArray();
                for (const auto& ram_value : final_ram_values) {
                    const auto& ram_value_array = ram_value.GetArray();
                    const auto address = static_cast<std::uint16_t>(ram_value_array[0].GetInt());
                    const auto expected = static_cast<std::uint8_t>(ram_value_array[1].GetInt());
                    const auto actual = memory->read(address);
                    ASSERT_EQUALS("Memory", expected, actual)
                }

                // TODO: Add cycle tests
            }
            std::cout << "PASSED" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }
    std::cout << "Every test file executed successfully." << std::endl;
    return 0;
}
