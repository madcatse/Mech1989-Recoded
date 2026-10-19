#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace mw::presentation {
struct DisplayResolution { int width, height; };
inline constexpr std::array<DisplayResolution, 8> displayResolutions{{
    {0, 0}, {1280, 800}, {1600, 900}, {1920, 1080}, {1920, 1200},
    {2560, 1440}, {2560, 1600}, {3840, 2160}
}};
struct DisplaySettings {
    int resolution = 0; // Auto: fit the window/monitor; fixed presets cap the canvas.
    bool fullscreen = false;
    bool mouseLook = false; // Local control preference; never part of a DOS save.
};
struct DisplayRect { int x, y, width, height; };
DisplaySettings loadDisplaySettings(const std::filesystem::path& path);
bool saveDisplaySettings(const std::filesystem::path& path, const DisplaySettings& settings);
std::wstring displayResolutionLabel(const DisplaySettings& settings);
DisplayRect pixelDisplayRect(int width, int height, const DisplaySettings& settings);
}
