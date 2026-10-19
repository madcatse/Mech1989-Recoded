#include "display_settings.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mw::presentation {
namespace {
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
int index(const DisplaySettings& settings) {
    return std::clamp(settings.resolution, 0, static_cast<int>(displayResolutions.size()) - 1);
}
}
DisplaySettings loadDisplaySettings(const std::filesystem::path& path) {
    DisplaySettings result;
    std::ifstream input(path);
    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') { section = line; continue; }
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
        if (section == "[controls]" && key == "mouselook") {
            if (value == "yes" || value == "1") result.mouseLook = true;
            else if (value == "no" || value == "0") result.mouseLook = false;
        }
        if (section != "[display]") continue;
        if (key == "fullscreen") {
            if (value == "yes" || value == "1") result.fullscreen = true;
            else if (value == "no" || value == "0") result.fullscreen = false;
        } else if (key == "resolution") {
            if (value == "auto") result.resolution = 0;
            for (size_t i = 1; i < displayResolutions.size(); ++i) {
                const auto r = displayResolutions[i];
                if (value == std::to_string(r.width) + "x" + std::to_string(r.height))
                    result.resolution = static_cast<int>(i);
            }
        }
    }
    return result;
}
bool saveDisplaySettings(const std::filesystem::path& path, const DisplaySettings& settings) {
    // Write beside the destination so replacement is atomic on the same volume.
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "; Mech1989: Recoded preferences (independent of DOS saves)\n[display]\nresolution=";
    const auto r = displayResolutions[index(settings)];
    if (!r.width) output << "auto";
    else output << r.width << 'x' << r.height;
    output << "\nfullscreen=" << (settings.fullscreen ? "yes" : "no") << '\n';
    output << "\n[controls]\nmouselook=" << (settings.mouseLook ? "yes" : "no") << '\n';
    output.close();
    const bool ok = !output.fail() && MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { std::error_code error; std::filesystem::remove(temporary, error); }
    return ok;
}
std::wstring displayResolutionLabel(const DisplaySettings& settings) {
    const auto r = displayResolutions[index(settings)];
    return r.width ? std::to_wstring(r.width) + L"X" + std::to_wstring(r.height) : L"AUTO";
}
DisplayRect pixelDisplayRect(int width, int height, const DisplaySettings& settings) {
    if (width <= 0 || height <= 0) return {0, 0, 0, 0};
    auto r = displayResolutions[index(settings)];
    const int availableW = r.width ? std::min(width, r.width) : width;
    const int availableH = r.height ? std::min(height, r.height) : height;
    // DOS artwork expects pixels 6/5 as tall as they are wide. Keep both axes
    // integer to avoid alternating stroke thickness: e.g. 4x5 on Full HD,
    // 5x6 at 1600x1200. The user prefers this earlier sharp approximation to
    // exact 4:3 with fractional resampling. A subnative client stays black.
    int scaleX = std::min(availableW / 320, availableH / 200);
    int scaleY = (6 * scaleX + 2) / 5;
    while (scaleY > availableH / 200) {
        --scaleX;
        scaleY = (6 * scaleX + 2) / 5;
    }
    const int w = 320 * scaleX, h = 200 * scaleY;
    return {(width - w) / 2, (height - h) / 2, w, h};
}
}
