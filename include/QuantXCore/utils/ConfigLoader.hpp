#pragma once

#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace QuantXCore::utils {

class ConfigLoader {
public:
    // Trims whitespace from both ends of a string
    static std::string trim(const std::string& str) {
        auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
            return std::isspace(ch);
        });
        auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
            return std::isspace(ch);
        }).base();

        return (start < end) ? std::string(start, end) : std::string();
    }

    // Loads environment variables from a specified .env file
    static void load_env_file(const std::string& filepath = ".env") {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return; // Fallback to system environment variables if .env is missing
        }

        std::string line;
        while (std::getline(file, line)) {
            // Trim leading/trailing whitespaces from the line
            std::string trimmed_line = trim(line);

            // Skip empty lines or comments
            if (trimmed_line.empty() || trimmed_line[0] == '#') {
                continue;
            }

            std::istringstream ss(trimmed_line);
            std::string key;
            if (std::getline(ss, key, '=')) {
                std::string value;
                if (std::getline(ss, value)) {
                    std::string clean_key = trim(key);
                    std::string clean_value = trim(value);

                    // Remove surrounding quotes if present (e.g., KEY="VALUE")
                    if (clean_value.length() >= 2) {
                        if ((clean_value.front() == '"' && clean_value.back() == '"') ||
                            (clean_value.front() == '\'' && clean_value.back() == '\'')) {
                            clean_value = clean_value.substr(1, clean_value.length() - 2);
                        }
                    }

                    if (!clean_key.empty()) {
                        // Set environment variable (overwrite = 1)
                        setenv(clean_key.c_str(), clean_value.c_str(), 1);
                    }
                }
            }
        }
    }

    // Retrieves an environment variable or a default value if not set
    static std::string get_env(const std::string& key, const std::string& default_val = "") {
        const char* val = std::getenv(key.c_str());
        return val ? std::string(val) : default_val;
    }
};

} // namespace QuantXCore::utils
