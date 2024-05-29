#include <string>
#include <iostream>
#include <iomanip>
#include <cstdint>

// Return human readable format like
// 2023-08-11 08:08:03.373
inline std::string get_timestamp(uint64_t milliseconds) {
    std::time_t seconds = milliseconds / 1000;
    std::tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &seconds); // Use localtime_s for Windows
#else
    localtime_r(&seconds, &timeinfo); // Use localtime_r for Linux/Unix
#endif

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << milliseconds % 1000;
    return oss.str();
}

// Return longer human readable format like
// Fri Aug 11 08:08:03 2023 CEST
inline std::string get_timestamp_long(uint64_t milliseconds) {
    std::time_t seconds = milliseconds / 1000;
    std::tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &seconds); // Use localtime_s for Windows
#else
    localtime_r(&seconds, &timeinfo); // Use localtime_r for Linux/Unix
#endif

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%c %Z");

    return oss.str();
}
