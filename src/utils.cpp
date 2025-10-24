#include "utils.h"

#include <fstream>
#include <stdexcept>

namespace sickboy {

    std::vector<std::uint8_t> FileUtils::read_binary(const std::filesystem::path& path) {
        std::ifstream file_handle(path, std::ios::binary);
        if (!file_handle) {
            throw std::runtime_error("Failed to open file at '" + path.string() + "'.");
        }
        file_handle.seekg(0, std::ios::end);
        auto file_size = file_handle.tellg();
        file_handle.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> result;
        result.resize(file_size);
        file_handle.read(reinterpret_cast<char*>(result.data()), file_size);
        return result;
    }

}