/* Copyright (c) 2024 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; version 2 of
   the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include <string>
#include <iostream>
#include <iomanip>
#include <cstdint>

/** Returns timestamp in the format like:
    2024-05-29 18:04:14.201  
*/
inline std::string get_timestamp(uint64_t milliseconds) {
    std::time_t seconds = milliseconds / 1000;
    std::tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &seconds); 
#else
    localtime_r(&seconds, &timeinfo); 
#endif

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << milliseconds % 1000;
    return oss.str();
}
/** Returns timestamp in the format like:
    Wed May 29 18:05:07 2024 EEST
*/

inline std::string get_timestamp_with_tz(uint64_t milliseconds) {
    std::time_t seconds = milliseconds / 1000;
    std::tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &seconds); 
#else
    localtime_r(&seconds, &timeinfo); 
#endif

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%c %Z");

    return oss.str();
}
