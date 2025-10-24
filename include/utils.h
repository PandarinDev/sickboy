#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <filesystem>

namespace sickboy {

    struct FileUtils {

        FileUtils() = delete;

        static std::vector<std::uint8_t> read_binary(const std::filesystem::path& path);

    };

    struct StringUtils {

        StringUtils() = delete;

        template<typename T>
        static std::string to_hex(const T& value) {
            std::stringstream stream;
            stream << std::hex << static_cast<int>(value);
            return stream.str();
        }

    };

}