#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <cwctype>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

fs::path executableDirectory();

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 200;
constexpr int kExactDisplayPixelWidth = 5;
constexpr int kExactDisplayPixelHeight = 6;
constexpr int kCompactDisplayPixelWidth = 4;
constexpr int kCompactDisplayPixelHeight = 5;
constexpr int kDefaultDisplayWidth = kScreenWidth * kCompactDisplayPixelWidth;
constexpr int kDefaultDisplayHeight = kScreenHeight * kCompactDisplayPixelHeight;
constexpr std::string_view kBuildDate = __DATE__;
constexpr std::string_view kBuildTime = __TIME__;

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct Image4bpp {
    int entryIndex = -1;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    std::vector<uint8_t> pixels;
};

struct Font {
    int width = 0;
    int height = 0;
    int firstCode = 0;
    int glyphCount = 0;
    std::vector<uint8_t> rows;

    bool contains(char ch) const {
        const int code = static_cast<unsigned char>(ch);
        return code >= firstCode && code < firstCode + glyphCount;
    }
};

struct PicsArchive {
    std::vector<Color> palette;
    std::vector<Image4bpp> images;
    size_t decodedSize = 0;
};

struct MwMainTextRef {
    std::string_view id;
    size_t fileOffset = 0;
    size_t length = 0;
    size_t expectedLineCount = 0;
};

struct PlanetRecord {
    uint16_t tableOrder = 0;
    uint16_t alphaOrder = 0;
    uint8_t planetNumber = 0;
    uint8_t houseId = 0;
    uint8_t terrainCode = 0;
    uint8_t unknownByte3 = 0;
    uint8_t contractAvailableFlag = 0;
    uint8_t mapX = 0;
    uint8_t mapY = 0;
    uint8_t unknownByte7 = 0;
    uint8_t economyTier = 1;
    uint64_t population = 0;
    std::string name;
    std::string description;
};

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ArmorOverlayRect {
    size_t section = 0;
    RectI rect;
};

static constexpr std::array<uint8_t, 145> kPlanetEconomyTiersByTableOrder = {{
    1, 2, 4, 2, 4, 2, 3, 4, 3, 2,
    2, 1, 2, 1, 1, 2, 2, 4, 2, 4,
    1, 3, 4, 1, 4, 3, 4, 1, 4, 4,
    1, 2, 2, 1, 2, 4, 2, 2, 1, 3,
    4, 1, 1, 2, 3, 2, 2, 4, 3, 4,
    4, 2, 4, 2, 3, 2, 3, 1, 1, 4,
    1, 2, 1, 3, 2, 2, 2, 2, 1, 2,
    4, 1, 4, 1, 3, 2, 2, 2, 1, 1,
    1, 4, 4, 1, 3, 2, 2, 2, 2, 1,
    1, 1, 2, 1, 1, 3, 1, 2, 3, 2,
    1, 1, 2, 1, 1, 2, 2, 4, 3, 1,
    2, 2, 3, 1, 2, 1, 1, 1, 2, 1,
    3, 1, 1, 4, 3, 1, 4, 4, 1, 2,
    3, 4, 2, 2, 4, 2, 3, 4, 2, 2,
    4, 1, 2, 3, 4,
}};

enum class ScreenState {
    ActivisionSplash,
    IntroText,
    Title,
    Authorization,
    CampaignMessage,
    MainMenu,
    StatusMenu,
    NewsNet,
    ContractMenu,
    ContractNegotiation,
    ContractAcceptedMessage,
    MechLabMenu,
    MechExtraAmmo,
    MechReviewList,
    MechStatus,
    MechRepairStatus,
    MechReloadPrompt,
    MechSellOffer,
    MechBuyList,
    MechBuyStatus,
    MechBuyDamageStatus,
    MechBuyCannotAfford,
    MechBuyTooMany,
    BarMenu,
    SystemMenu,
    SaveGameNameInput,
    RestoreGameList,
    CrewMenu,
    Starmap,
    TravelRoutePreview,
    TravelAnimation,
    MissionBattleStub,
    MissionDebrief,
};

enum class BarDialogState {
    None,
    RecruitOffer,
    NoCandidates,
    CrewFull,
};

enum class StarmapMenuMode {
    None,
    Houses,
    HousePlanets,
};

enum class StoryBackdrop {
    Campaign,
    Bar,
    Contract,
    MechLab,
};

enum class PlanetEnvironment {
    Desert,
    Tropical,
    Ice,
};

    enum class CrewInteractionMode {
        Navigate,
        AssignMech,
    };

    enum class MissionOutcome {
        Victory,
        Defeat,
        Death,
    };

static const Color kEgaPalette[16] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

static const uint8_t kPicsSourceToGamePaletteIndex[16] = {
    5, 0, 15, 7,
    8, 11, 9, 1,
    12, 4, 10, 3,
    2, 14, 6, 13,
};

uint16_t readU16Le(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("unexpected end of data while reading u16");
    }
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t readPicsOffset(const std::vector<uint8_t>& decoded, size_t offset) {
    const uint32_t high = readU16Le(decoded, offset);
    const uint32_t low = readU16Le(decoded, offset + 2);
    return (high << 16) | low;
}

std::wstring widen(const char* text) {
    if (!text) {
        return {};
    }
    std::wstring result;
    while (*text) {
        result.push_back(static_cast<unsigned char>(*text++));
    }
    return result;
}

std::wstring widen(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<unsigned char>(ch));
    }
    return result;
}

std::wstring buildTimestampLine() {
    return L"Build timestamp: " + widen(kBuildDate) + L" " + widen(kBuildTime);
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open file");
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("unable to measure file");
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!input && !data.empty()) {
        throw std::runtime_error("unable to read whole file");
    }
    return data;
}

std::string utf8FromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
// MW_DEBUG_TOOLS: development-only logger. Disable MW_ENABLE_DEBUG_TOOLS in CMake for release builds.
class DebugTools {
public:
    bool initialize(const fs::path& exeDir) {
        logPath_ = exeDir / L"mwlog.txt";
        file_.open(logPath_, std::ios::binary | std::ios::trunc);

        if (!GetConsoleWindow()) {
            AllocConsole();
        }
        consoleOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (consoleOut_ != INVALID_HANDLE_VALUE && consoleOut_ != nullptr) {
            SetConsoleTitleW(L"MW_MAIN Recomp Debug Log");
            SetConsoleTextAttribute(
                consoleOut_,
                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }

        initialized_ = true;
        log(L"Debug tools enabled. Log file: " + logPath_.wstring());
        return file_.is_open() || (consoleOut_ != INVALID_HANDLE_VALUE && consoleOut_ != nullptr);
    }

    void shutdown() {
        if (!initialized_) {
            return;
        }
        log(L"Debug tools shutting down.");
        file_.close();
        if (GetConsoleWindow()) {
            FreeConsole();
        }
        initialized_ = false;
    }

    void log(std::wstring_view message) {
        if (!initialized_) {
            return;
        }

        const std::wstring line = timestamp() + L" " + std::wstring(message);
        recentLines_.push_back(line);
        if (recentLines_.size() > kMaxRecentLines) {
            recentLines_.erase(recentLines_.begin());
        }

        if (file_) {
            file_ << utf8FromWide(line) << "\r\n";
            file_.flush();
        }

        if (consoleOut_ != INVALID_HANDLE_VALUE && consoleOut_ != nullptr) {
            std::wstring consoleLine = line + L"\r\n";
            DWORD written = 0;
            WriteConsoleW(
                consoleOut_,
                consoleLine.data(),
                static_cast<DWORD>(consoleLine.size()),
                &written,
                nullptr);
        }
    }

    const std::vector<std::wstring>& recentLines() const {
        return recentLines_;
    }

private:
    static std::wstring timestamp() {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t buffer[32] = {};
        swprintf_s(
            buffer,
            L"[%02u:%02u:%02u]",
            static_cast<unsigned>(time.wHour),
            static_cast<unsigned>(time.wMinute),
            static_cast<unsigned>(time.wSecond));
        return buffer;
    }

    static constexpr size_t kMaxRecentLines = 64;
    bool initialized_ = false;
    HANDLE consoleOut_ = nullptr;
    fs::path logPath_;
    std::ofstream file_;
    std::vector<std::wstring> recentLines_;
};
#endif

std::vector<uint8_t> decompressPicsNibbleRle(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> decodedNibbles;
    decodedNibbles.reserve(data.size() * 2);

    size_t nibbleIndex = 0;
    const auto nextNibble = [&]() -> int {
        if (nibbleIndex >= data.size() * 2) {
            return -1;
        }
        const uint8_t byte = data[nibbleIndex / 2];
        const int value = (nibbleIndex % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
        ++nibbleIndex;
        return value;
    };

    while (true) {
        const int nibble = nextNibble();
        if (nibble < 0) {
            break;
        }
        if (nibble != 0x0F) {
            decodedNibbles.push_back(static_cast<uint8_t>(nibble));
            continue;
        }
        const int countMinusOne = nextNibble();
        const int value = nextNibble();
        if (countMinusOne < 0 || value < 0) {
            break;
        }
        decodedNibbles.insert(
            decodedNibbles.end(),
            static_cast<size_t>(countMinusOne + 1),
            static_cast<uint8_t>(value));
    }

    std::vector<uint8_t> decoded;
    decoded.reserve(decodedNibbles.size() / 2);
    for (size_t i = 0; i + 1 < decodedNibbles.size(); i += 2) {
        decoded.push_back(static_cast<uint8_t>((decodedNibbles[i] << 4) | decodedNibbles[i + 1]));
    }
    return decoded;
}

Color dac6ToRgb(uint8_t r, uint8_t g, uint8_t b) {
    const auto expand = [](uint8_t value) -> uint8_t {
        const int clamped = std::min<int>(63, value);
        return static_cast<uint8_t>((clamped * 255 + 31) / 63);
    };
    return {expand(r), expand(g), expand(b)};
}

PicsArchive loadPicsArchive(const fs::path& path) {
    const std::vector<uint8_t> packed = readFile(path);
    const std::vector<uint8_t> decoded = decompressPicsNibbleRle(packed);
    if (decoded.size() < 8) {
        throw std::runtime_error("PICS archive decoded data is too small");
    }

    const uint32_t headerSize = readPicsOffset(decoded, 0);
    if (headerSize == 0 || headerSize > decoded.size() || headerSize % 6 != 0) {
        throw std::runtime_error("PICS archive header was not recognized");
    }

    const size_t entryCount = headerSize / 6;
    if (entryCount > 512 || entryCount * 6 > decoded.size()) {
        throw std::runtime_error("PICS archive entry table is out of range");
    }

    std::vector<uint32_t> offsets(entryCount);
    std::vector<uint16_t> sizes(entryCount);
    for (size_t i = 0; i < entryCount; ++i) {
        offsets[i] = readPicsOffset(decoded, i * 4);
    }
    const size_t sizesOffset = entryCount * 4;
    for (size_t i = 0; i < entryCount; ++i) {
        sizes[i] = readU16Le(decoded, sizesOffset + i * 2);
    }

    PicsArchive archive;
    archive.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
    archive.decodedSize = decoded.size();

    for (size_t i = 0; i < entryCount; ++i) {
        const uint32_t offset = offsets[i];
        const uint16_t size = sizes[i];
        if (offset == 0 && size == 0) {
            continue;
        }
        if (offset < headerSize || offset + size > decoded.size()) {
            continue;
        }

        if (size == 48) {
            std::vector<Color> palette;
            palette.reserve(16);
            for (size_t color = 0; color < 16; ++color) {
                const size_t base = offset + color * 3;
                palette.push_back(dac6ToRgb(decoded[base], decoded[base + 1], decoded[base + 2]));
            }
            archive.palette = std::move(palette);
            continue;
        }

        if (size < 9) {
            continue;
        }
        const int width = readU16Le(decoded, offset);
        const int height = readU16Le(decoded, offset + 2);
        if (width <= 0 || width > 640 || height <= 0 || height > 400) {
            continue;
        }
        const int rowStride = (width + 1) / 2;
        const size_t pixelBytes = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
        const size_t pixelOffset = offset + 4;
        if (pixelOffset + pixelBytes > offset + size) {
            continue;
        }

        Image4bpp image;
        image.entryIndex = static_cast<int>(i);
        image.width = width;
        image.height = height;
        image.rowStride = rowStride;
        image.pixels.assign(decoded.begin() + pixelOffset, decoded.begin() + pixelOffset + pixelBytes);
        archive.images.push_back(std::move(image));
    }

    return archive;
}

PicsArchive loadRawPicsImage(const fs::path& path, uint32_t offset, int entryIndex) {
    const std::vector<uint8_t> packed = readFile(path);
    const std::vector<uint8_t> decoded = decompressPicsNibbleRle(packed);
    if (offset + 4 > decoded.size()) {
        throw std::runtime_error("raw PICS offset is out of range");
    }

    const int width = readU16Le(decoded, offset);
    const int storedHeight = readU16Le(decoded, offset + 2);
    const int height = storedHeight & 0x7FFF;
    if (width <= 0 || width > 640 || height <= 0 || height > 400) {
        throw std::runtime_error("raw PICS dimensions were not recognized");
    }

    const int rowStride = (width + 1) / 2;
    const size_t pixelBytes = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    const size_t pixelOffset = static_cast<size_t>(offset) + 4;
    if (pixelOffset + pixelBytes > decoded.size()) {
        throw std::runtime_error("raw PICS pixels are out of range");
    }

    Image4bpp image;
    image.entryIndex = entryIndex;
    image.width = width;
    image.height = height;
    image.rowStride = rowStride;
    image.pixels.assign(decoded.begin() + pixelOffset, decoded.begin() + pixelOffset + pixelBytes);

    PicsArchive archive;
    archive.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
    archive.decodedSize = decoded.size();
    archive.images.push_back(std::move(image));
    return archive;
}

PicsArchive loadRawPicsImageWithoutHeader(
    const fs::path& path,
    uint32_t pixelOffset,
    int entryIndex,
    int width,
    int height) {
    const std::vector<uint8_t> packed = readFile(path);
    const std::vector<uint8_t> decoded = decompressPicsNibbleRle(packed);
    if (width <= 0 || width > 640 || height <= 0 || height > 400) {
        throw std::runtime_error("raw PICS dimensions were not recognized");
    }

    const int rowStride = (width + 1) / 2;
    const size_t pixelBytes = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    if (static_cast<size_t>(pixelOffset) + pixelBytes > decoded.size()) {
        throw std::runtime_error("raw PICS pixels are out of range");
    }

    Image4bpp image;
    image.entryIndex = entryIndex;
    image.width = width;
    image.height = height;
    image.rowStride = rowStride;
    image.pixels.assign(decoded.begin() + pixelOffset, decoded.begin() + pixelOffset + pixelBytes);

    PicsArchive archive;
    archive.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
    archive.decodedSize = decoded.size();
    archive.images.push_back(std::move(image));
    return archive;
}

PicsArchive loadRawPicsImageWithNibblePhase(
    const fs::path& path,
    uint32_t offset,
    int entryIndex,
    int width,
    int height,
    bool hasHeader,
    int nibblePhase) {
    const std::vector<uint8_t> packed = readFile(path);
    const std::vector<uint8_t> decoded = decompressPicsNibbleRle(packed);
    if (width <= 0 || width > 640 || height <= 0 || height > 400) {
        throw std::runtime_error("raw PICS dimensions were not recognized");
    }

    const int rowStride = (width + 1) / 2;
    const size_t pixelBytes = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    const size_t pixelOffset = static_cast<size_t>(offset) + (hasHeader ? 4u : 0u);
    const int phase = nibblePhase & 1;
    if (pixelOffset + (pixelBytes + 1u) > decoded.size()) {
        throw std::runtime_error("raw PICS pixels are out of range");
    }

    std::vector<uint8_t> pixels(pixelBytes, 0);
    size_t nibbleIndex = static_cast<size_t>(phase);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t sourceOffset = pixelOffset + (nibbleIndex >> 1);
            const uint8_t sourceByte = decoded[sourceOffset];
            const uint8_t colorIndex = (nibbleIndex & 1u) != 0u
                ? static_cast<uint8_t>(sourceByte & 0x0F)
                : static_cast<uint8_t>((sourceByte >> 4) & 0x0F);
            uint8_t& targetByte = pixels[static_cast<size_t>(y) * rowStride + x / 2];
            if ((x & 1) == 0) {
                targetByte = static_cast<uint8_t>((colorIndex << 4) | (targetByte & 0x0F));
            } else {
                targetByte = static_cast<uint8_t>((targetByte & 0xF0) | colorIndex);
            }
            ++nibbleIndex;
        }
        if ((width & 1) != 0 && static_cast<int>(nibbleIndex & 1u) != phase) {
            ++nibbleIndex;
        }
    }

    Image4bpp image;
    image.entryIndex = entryIndex;
    image.width = width;
    image.height = height;
    image.rowStride = rowStride;
    image.pixels = std::move(pixels);

    PicsArchive archive;
    archive.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
    archive.decodedSize = decoded.size();
    archive.images.push_back(std::move(image));
    return archive;
}

Font loadFont(const fs::path& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.size() >= 8 && data[0] == 'F' && data[1] == 'N' && data[2] == 'T' && data[3] == ':') {
        const uint32_t payloadSize =
            static_cast<uint32_t>(data[4]) |
            (static_cast<uint32_t>(data[5]) << 8) |
            (static_cast<uint32_t>(data[6]) << 16) |
            (static_cast<uint32_t>(data[7]) << 24);
        if (8u + payloadSize <= data.size()) {
            data = std::vector<uint8_t>(data.begin() + 8, data.begin() + 8 + payloadSize);
        }
    }
    if (data.size() < 4) {
        throw std::runtime_error("FNT payload is too small");
    }

    Font font;
    font.width = data[0];
    font.height = data[1];
    font.firstCode = data[2];
    font.glyphCount = data[3];
    const size_t expected = 4 + static_cast<size_t>(font.glyphCount) * static_cast<size_t>(font.height);
    if (font.width <= 0 || font.width > 8 || font.height <= 0 || font.height > 16 || expected != data.size()) {
        throw std::runtime_error("FNT payload was not recognized");
    }
    font.rows.assign(data.begin() + 4, data.end());
    return font;
}

std::vector<std::wstring> parseMwMainTextBlock(const std::vector<uint8_t>& data, const MwMainTextRef& textRef) {
    if (textRef.fileOffset + textRef.length > data.size()) {
        throw std::runtime_error("MW_MAIN.EXE text block is out of range: " + std::string(textRef.id));
    }

    std::vector<std::wstring> lines;
    std::wstring line;
    bool previousWasCr = false;
    const size_t end = textRef.fileOffset + textRef.length;
    for (size_t offset = textRef.fileOffset; offset < end; ++offset) {
        const uint8_t value = data[offset];
        if (value == '\r' || value == '\n') {
            if (value == '\n' && previousWasCr) {
                previousWasCr = false;
                continue;
            }
            lines.push_back(std::move(line));
            line.clear();
            previousWasCr = value == '\r';
            continue;
        }
        previousWasCr = false;

        if (value == 0) {
            break;
        }
        if ((value < 0x20 || value > 0x7E) && value != '\t') {
            throw std::runtime_error("MW_MAIN.EXE text block contains non-ASCII data: " + std::string(textRef.id));
        }
        line.push_back(static_cast<wchar_t>(value));
    }
    lines.push_back(std::move(line));

    if (textRef.expectedLineCount != 0 && lines.size() != textRef.expectedLineCount) {
        throw std::runtime_error("MW_MAIN.EXE text block line count mismatch: " + std::string(textRef.id));
    }
    return lines;
}

std::vector<std::wstring> loadMwMainTextBlock(const fs::path& path, const MwMainTextRef& textRef) {
    return parseMwMainTextBlock(readFile(path), textRef);
}

std::string readCStringAscii(const std::vector<uint8_t>& data, size_t offset, size_t& nextOffset) {
    if (offset >= data.size()) {
        throw std::runtime_error("string offset is out of range");
    }
    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, data.end(), 0);
    if (end == data.end()) {
        throw std::runtime_error("unterminated ASCII string");
    }

    std::string text(begin, end);
    for (char ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (value < 0x20 || value > 0x7E) {
            throw std::runtime_error("planet string contains non-ASCII data");
        }
    }
    nextOffset = static_cast<size_t>(std::distance(data.begin(), end)) + 1u;
    return text;
}

PlanetRecord parsePlanetRecord(const std::vector<uint8_t>& data, uint16_t pointerValue, uint16_t tableOrder) {
    constexpr size_t kPointerBase = 0x008D00u;
    const size_t offset = kPointerBase + pointerValue;
    if (offset + 10u >= data.size()) {
        throw std::runtime_error("planet record is out of range");
    }

    PlanetRecord record;
    record.tableOrder = tableOrder;
    record.planetNumber = data[offset + 0u];
    record.houseId = data[offset + 1u];
    record.terrainCode = data[offset + 2u];
    record.unknownByte3 = data[offset + 3u];
    record.contractAvailableFlag = data[offset + 4u];
    record.mapX = data[offset + 5u];
    record.mapY = data[offset + 6u];
    record.unknownByte7 = data[offset + 7u];
    if (tableOrder > 0 && tableOrder <= kPlanetEconomyTiersByTableOrder.size()) {
        record.economyTier = kPlanetEconomyTiersByTableOrder[tableOrder - 1u];
    }
    record.population = static_cast<uint64_t>(readU16Le(data, offset + 8u)) * 1000000ull;

    if (record.planetNumber == 0 || record.houseId > 4 || record.terrainCode == 0 || record.terrainCode > 12) {
        throw std::runtime_error("planet record header was not recognized");
    }

    size_t afterName = 0;
    record.name = readCStringAscii(data, offset + 10u, afterName);
    size_t afterDescription = 0;
    record.description = readCStringAscii(data, afterName, afterDescription);
    (void)afterDescription;
    if (record.name.empty() || record.description.empty()) {
        throw std::runtime_error("planet record has an empty name or description");
    }
    return record;
}

std::vector<PlanetRecord> loadPlanetRecords(const fs::path& path) {
    constexpr size_t kPointerTableOffset = 0x00F270u;
    constexpr size_t kPrimaryPointerCount = 146u;
    constexpr size_t kAlphaPointerStartIndex = 146u;
    constexpr size_t kAlphaPointerScanCount = 145u;
    constexpr size_t kUniquePlanetCount = 145u;

    const std::vector<uint8_t> data = readFile(path);
    std::vector<PlanetRecord> planets;
    planets.reserve(kUniquePlanetCount);
    std::unordered_set<std::string> seenNames;

    for (size_t index = 0; index < kPrimaryPointerCount; ++index) {
        const uint16_t pointerValue = readU16Le(data, kPointerTableOffset + index * 2u);
        PlanetRecord record = parsePlanetRecord(data, pointerValue, static_cast<uint16_t>(index + 1u));
        if (!seenNames.insert(record.name).second) {
            continue;
        }
        planets.push_back(std::move(record));
    }
    if (planets.size() != kUniquePlanetCount) {
        throw std::runtime_error("expected 145 unique planets in MW_MAIN.EXE");
    }

    uint16_t alphaOrder = 1;
    for (size_t index = kAlphaPointerStartIndex; index < kAlphaPointerStartIndex + kAlphaPointerScanCount; ++index) {
        const uint16_t pointerValue = readU16Le(data, kPointerTableOffset + index * 2u);
        const PlanetRecord candidate = parsePlanetRecord(data, pointerValue, 0);
        auto it = std::find_if(
            planets.begin(),
            planets.end(),
            [&candidate](const PlanetRecord& record) { return record.name == candidate.name; });
        if (it != planets.end()) {
            it->alphaOrder = alphaOrder++;
        }
    }

    return planets;
}

uint32_t toBgra(Color color) {
    return 0xFF000000u |
        (static_cast<uint32_t>(color.r) << 16) |
        (static_cast<uint32_t>(color.g) << 8) |
        static_cast<uint32_t>(color.b);
}

class App {
public:
    explicit App(fs::path resourceRoot) : resourceRoot_(std::move(resourceRoot)) {
        framebuffer_.assign(kScreenWidth * kScreenHeight, 0xFF000000u);
    }

    ~App() {
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugTools_.shutdown();
#endif
    }

    bool initialize(HINSTANCE instance, int showCommand) {
        instance_ = instance;
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugTools_.initialize(executableDirectory());
        debugLog(buildTimestampLine());
        debugLog(L"Application initialize. Resource root: " + resourceRoot_.wstring());
#endif
        loadResources();

        WNDCLASSW wc{};
        wc.lpfnWndProc = &App::windowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"MWMainRecompWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassW(&wc)) {
            MessageBoxW(nullptr, L"Unable to register window class.", L"MW Main Recomp", MB_ICONERROR);
            return false;
        }

        RECT rect{0, 0, kDefaultDisplayWidth, kDefaultDisplayHeight};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = CreateWindowExW(
            0,
            wc.lpszClassName,
            L"MW_MAIN replacement engine",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!hwnd_) {
            MessageBoxW(nullptr, L"Unable to create window.", L"MW Main Recomp", MB_ICONERROR);
            return false;
        }

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        SetTimer(hwnd_, 1, 16, nullptr);
        return true;
    }

    int run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    enum class MenuAction {
        None,
        Crew,
        RecruitCrew,
        NewsNet,
        RequestMission,
        SystemMenu,
        ToggleSound,
        Detail,
        Restart,
        Continue,
        SaveGame,
        RestoreGame,
        ReviewMechs,
        ExtraAmmo,
        BuyMechs,
        ExitToDos,
    };

    enum class StoryAction {
        None,
        GameOver,
        RestartCampaign,
        QuitToDos,
        GrigYes,
        GrigNo,
        DustballFirstFight,
        DustballFirstRun,
        DustballSecondFight,
        DustballFightThenRun,
        DustballRunThenRun,
        SniperFight,
        SniperRun,
        FollowAddress,
        ForgetAddress,
        OfficeHide,
        OfficeFight,
        OfficeTalk,
        OfficeHideFight,
        OfficeHideRun,
        AcceptBlackWidowStory,
        ChallengeBlackWidowStory,
        FollowTasha,
        StayDown,
        TrustTasha,
        TrustKearney,
        FinalAttack,
        FinalDelay,
    };

    struct MenuItem {
        std::wstring_view label;
        MenuAction action = MenuAction::None;
    };

    struct StoryChoice {
        std::wstring label;
        StoryAction action = StoryAction::None;
    };

    struct StoryPage {
        std::vector<std::wstring> lines;
        std::vector<StoryChoice> choices;
    };

    struct CrewMember {
        bool hired = false;
        std::wstring_view name;
        std::wstring_view gunnery;
        std::wstring_view piloting;
        uint32_t wage = 0;
        int portraitEntry = -1;
        int recruitIndex = -1;
        uint8_t missionExperience = 0;
    };

    struct MissionParticipant {
        int crewSlot = -1;
        int mechIndex = -1;
        std::wstring name;
        std::wstring mechName;
        int armorPercent = 100;
        bool killed = false;
    };

    enum class DamageState {
        Functional,
        LightDamage,
        HeavyDamage,
        Junk,
    };

    enum class ChassisId : size_t {
        Locust,
        Wasp,
        Jenner,
        PhoenixHawk,
        ShadowHawk,
        Wolverine,
        Rifleman,
        Warhammer,
        Marauder,
        Battlemaster,
    };

    enum class RepairTargetKind {
        Engine,
        Gyros,
        Sensors,
        LifeSupport,
        HeatSink,
        LeftArmActuator,
        RightArmActuator,
        LeftLegActuator,
        RightLegActuator,
        JumpJets,
        Armor,
        Weapon,
    };

    struct RepairTarget {
        RepairTargetKind kind = RepairTargetKind::Engine;
        size_t index = 0;
    };

    struct MechWeaponStatus {
        std::wstring_view weapon;
        std::wstring_view location;
        DamageState condition = DamageState::Functional;
    };

    struct OwnedMech {
        ChassisId chassis = ChassisId::Jenner;
        std::wstring_view name;
        int imageEntry = -1;
        int assignedCrewSlot = -1;
        int statusImageEntry = -1;
        DamageState condition = DamageState::Functional;
        uint32_t repairCost = 0;
        int tons = 0;
        int speedKph = 0;
        int jumpCapMeters = 0;
        int heatSinksWorking = 0;
        int heatSinksTotal = 0;
        int jumpJetsWorking = 0;
        int jumpJetsTotal = 0;
        int armorPercent = 100;
        std::array<int, 9> armorDamage = {};
        std::array<int, 6> ammoPacks = {};
        DamageState engine = DamageState::Functional;
        DamageState gyros = DamageState::Functional;
        DamageState sensors = DamageState::Functional;
        DamageState lifeSupport = DamageState::Functional;
        DamageState leftArmActuator = DamageState::Functional;
        DamageState rightArmActuator = DamageState::Functional;
        DamageState leftLegActuator = DamageState::Functional;
        DamageState rightLegActuator = DamageState::Functional;
        std::array<MechWeaponStatus, 10> weapons = {};
    };

    enum class CrewAssignmentOptionKind {
        Dismiss,
        Mech,
        None,
    };

    struct CrewAssignmentOption {
        CrewAssignmentOptionKind kind = CrewAssignmentOptionKind::None;
        size_t mechIndex = 0;
    };

    struct PilotTextDefinition {
        std::string_view id;
        MwMainTextRef name;
        MwMainTextRef quote;
    };

    struct RecruitPilot {
        std::string_view id;
        std::wstring name;
        std::vector<std::wstring> quoteLines;
        uint8_t portraitEntry = 0;
        uint8_t gunnerySkill = 0;
        uint8_t pilotingSkill = 0;
        uint32_t monthlyWage = 0;
    };

    struct PlanetRecruitPool {
        int visitSerial = -1;
        int monthKey = -1;
        std::vector<size_t> originalCandidates;
        std::vector<size_t> candidates;
    };

    struct MarketMech {
        OwnedMech mech;
        uint32_t askingPrice = 0;
    };

    struct PlanetMechMarket {
        bool initialized = false;
        std::vector<MarketMech> mechsForSale;
    };

    struct SaveGameSlot {
        fs::path path;
        std::wstring name;
        bool occupied = false;
    };

    struct AmmoDefinition {
        std::wstring_view label;
        uint32_t tierOneCost = 0;
        std::array<std::wstring_view, 4> compatibleMechs = {};
        size_t compatibleMechCount = 0;
    };

    struct MechImageSpec {
        int entry = -1;
        uint32_t offset = 0;
        int width = 0;
        int height = 0;
        int nibblePhase = 0;
    };

    struct ContractImageSpec {
        uint32_t offset = 0;
        int width = 0;
        int height = 0;
        int nibblePhase = 0;
    };

    enum class ContractEditableField {
        None,
        Price,
        Salvage,
        Advance,
    };

    struct ContractMissionDefinition {
        std::wstring_view name;
        std::wstring_view family;
        bool extended = false;
    };

    struct ContractOffer {
        uint8_t employerHouse = 0;
        uint8_t targetHouse = 0;
        bool hasHostileTargetHouse = true;
        std::wstring targetPlanet;
        std::wstring_view missionName;
        int heavyCount = 0;
        int mediumCount = 0;
        int lightCount = 1;
        int priceK = 100;
        int salvagePercent = 0;
        int advancePercent = 0;
        int housePriceK = 100;
        int houseSalvagePercent = 0;
        int houseAdvancePercent = 0;
        int negotiationRounds = 0;
        bool termsModified = false;
    };

    struct MechDefinition {
        ChassisId chassis = ChassisId::Jenner;
        std::wstring_view name;
        MechImageSpec crewImage;
        MechImageSpec statusImage;
        int tons = 0;
        int speedKph = 0;
        int jumpCapMeters = 0;
        int heatSinks = 0;
        int jumpJets = 0;
        std::array<MechWeaponStatus, 10> weapons = {};
        std::array<uint32_t, 4> buyPrices = {};
        std::array<uint32_t, 4> sellPrices = {};
        uint32_t marketWeight = 1;
    };

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
    struct CheatDefinition {
        std::string_view name;
        void (App::*execute)();
    };
#endif

    static constexpr size_t kPlanetStatusIconIndex = 0;
    static constexpr size_t kPlanetMechLabIconIndex = 1;
    static constexpr size_t kPlanetContractIconIndex = 2;
    static constexpr size_t kPlanetStarmapIconIndex = 3;
    static constexpr size_t kPlanetBarIconIndex = 4;
    static constexpr size_t kPlanetSystemIconIndex = 5;
    static constexpr size_t kSystemSaveMenuIndex = 0;
    static constexpr size_t kSystemRestoreMenuIndex = 1;
    static constexpr size_t kSystemContinueMenuIndex = 6;
    static constexpr size_t kRestoreGameCancelIndex = 12;
    static constexpr size_t kGamSaveSize = 0x718;
    static constexpr size_t kGamMaxNameChars = 8;
    static constexpr size_t kGamVisibleSlotCount = 12;
    static constexpr size_t kMwMainNewGameTemplateFileOffset = 0x009148;
    static constexpr size_t kGamOffsetReputation = 0x001D;
    static constexpr size_t kGamOffsetPlanetIndex = 0x0021;
    static constexpr size_t kGamOffsetCurrentPlanetHouseId = 0x0025;
    static constexpr size_t kGamOffsetCurrentPlanetTerrainBand = 0x0027;
    static constexpr size_t kGamOffsetCurrentPlanetContractAvailable = 0x0029;
    static constexpr size_t kGamOffsetMapX = 0x002B;
    static constexpr size_t kGamOffsetMapY = 0x002D;
    static constexpr size_t kGamOffsetMonthDayCounter = 0x0031;
    static constexpr size_t kGamOffsetMonth = 0x0033;
    static constexpr size_t kGamOffsetYear = 0x0035;
    static constexpr size_t kGamOffsetPeriodic14DayCounter = 0x0037;
    static constexpr size_t kGamOffsetMoney = 0x0049;
    static constexpr size_t kGamOffsetFamilyAttitudes = 0x004D;
    static constexpr size_t kGamOffsetPositiveHouseCounters = 0x0057;
    static constexpr size_t kGamOffsetNegativeHouseCounters = 0x0061;
    static constexpr size_t kGamOffsetReputationPoints = 0x006B;
    static constexpr size_t kGamOffsetCrewCount = 0x006D;
    static constexpr size_t kGamOffsetCrewPilotIds = 0x006F;
    static constexpr size_t kGamOffsetReputationTier = 0x00A2;
    static constexpr size_t kGamOffsetCrewGunnerySkills = 0x00AA;
    static constexpr size_t kGamOffsetCrewPilotingSkills = 0x00B2;
    static constexpr size_t kGamOffsetCrewAssignedMechs = 0x00C2;
    static constexpr size_t kGamOffsetMechCount = 0x00E8;
    static constexpr size_t kGamOffsetMechChassisList = 0x00EA;
    static constexpr size_t kGamOffsetMechRecords = 0x0102;
    static constexpr size_t kGamMechRecordStride = 0x1D;
    static constexpr size_t kGamOffsetMechAmmo = 0x025E;
    static constexpr size_t kGamOffsetExtraAmmo = 0x02EE;
    static constexpr size_t kGamOffsetSoundDisabled = 0x05E2;
    static constexpr size_t kGamOffsetMessageFlags = 0x0672;
    static constexpr size_t kGamKnownMessageFlagCount = 0x8C;
    static constexpr size_t kGamOffsetDetailLevel = 0x070D;
    static constexpr int kCommanderBirthYear = 3006;
    static constexpr int kCommanderBirthMonth = 4;
    static constexpr int kCommanderBirthDay = 8;
    static constexpr int kStartingYear = 3024;
    static constexpr int kStartingMonthZeroBased = 3;
    static constexpr int kStartingMonthDayCounter = 1;
    static constexpr int kStartingPeriodic14DayCounter = 0;
    static constexpr int kCampaignDaysPerMonth = 60;
    static constexpr int kCampaignMonthsPerYear = 12;
    static constexpr int kPeriodicCampaignUpdateDays = 14;
    static constexpr uint32_t kMissionHostileBaseDuration = 82;
    static constexpr uint32_t kMissionGarrisonBaseDuration = 150;
    static constexpr uint64_t kMaxPlayerWealth = 10000000000ull;
    static constexpr int kMechLabBackgroundEntry = 2;
    static constexpr int kCampaignDesertEntry = 1;
    static constexpr int kCampaignTropicalEntry = 2;
    static constexpr int kCampaignIceEntry = 3;
    static constexpr int kBarBaseEntry = 2;
    static constexpr int kBarTropicalOverlayEntry = 5;
    static constexpr int kBarIceOverlayEntry = 6;
    static constexpr int kMechLabWeldX = 96;
    static constexpr int kMechLabWeldY = 64;
    static constexpr DWORD kMechLabWeldFrameMs = 60;
    static constexpr int kMechLabWeldEndingEntry = 33;
    static constexpr int kStarmapBackgroundEntry = 1;
    static constexpr int kCrewPlayerPortraitEntry = 22;
    static constexpr int kCrewDecorationEntry = 26;
    static constexpr size_t kPlayableMechCount = 10;
    static constexpr size_t kMaxMechWeaponRows = 10;
    static constexpr int kMechReviewPanelX = 48;
    static constexpr int kMechReviewPanelY = 43;
    static constexpr int kMechReviewPanelWidth = 224;
    static constexpr int kMechReviewTitleY = 48;
    static constexpr int kMechReviewItemY = 60;
    static constexpr int kMechReviewLineStep = 10;
    static constexpr RectI kExtraAmmoCompatPanelRect{0, 0, 96, 64};
    static constexpr RectI kExtraAmmoMainPanelRect{50, 104, 215, 90};
    static constexpr int kExtraAmmoTitleY = 110;
    static constexpr int kExtraAmmoHeaderY = 122;
    static constexpr int kExtraAmmoFirstRowY = 134;
    static constexpr int kExtraAmmoLineStep = 6;
    static constexpr int kExtraAmmoAmmoTypeX = 70;
    static constexpr int kExtraAmmoCostX = 150;
    static constexpr int kExtraAmmoHoldX = 217;
    static constexpr int kExtraAmmoDoneY = 174;
    static constexpr int kExtraAmmoWealthY = 185;
    static constexpr int kExtraAmmoMaxInHold = 9999;
    static constexpr int kMechStatusTextAreaWidth = 171;
    static constexpr int kMechStatusTextX = 14;
    static constexpr int kMechStatusTitleY = 12;
    static constexpr int kMechStatusFirstLineY = 24;
    static constexpr int kMechStatusLineStep = 12;
    static constexpr int kMechStatusGridX = 171;
    static constexpr int kMechStatusGridY = 0;
    static constexpr int kMechStatusGridColumns = 15;
    static constexpr int kMechStatusGridRows = 20;
    static constexpr int kMechStatusGridCellSize = 9;
    static constexpr int kMechStatusGridLineSize = 1;
    static constexpr int kMechStatusImageX = 183;
    static constexpr int kMechStatusImageY = 15;
    static constexpr int kMechRepairTitleY = 8;
    static constexpr int kMechRepairFirstLineY = 18;
    static constexpr int kMechRepairComponentLineStep = 6;
    static constexpr int kMechRepairWeaponHeaderY = 87;
    static constexpr int kMechRepairWeaponFirstRowY = 96;
    static constexpr int kMechRepairDoneY = 164;
    static constexpr int kMechRepairCostY = 173;
    static constexpr int kMechRepairWealthY = 185;
    static constexpr int kMechRepairCostValueX = 105;
    static constexpr int kMechRepairCostValueWidth = 60;
    static constexpr uint32_t kMechRepairComponentMultiplier = 333;
    static constexpr uint32_t kMechRepairCountUnitCost = 2000;
    static constexpr uint32_t kMechRepairWeaponLightCost = 2000;
    static constexpr uint32_t kMechRepairArmorLevelCost = 4000;
    static constexpr size_t kArmorSectionCount = 9;
    static constexpr int kArmorDamageMaxLevel = 3;
    static constexpr int kArmorDamageDenominator =
        static_cast<int>(kArmorSectionCount) * kArmorDamageMaxLevel;
    static constexpr int kMechAmmoMaxPacks = 25;
    static constexpr RectI kMechStatusRepairButtonRect{58, 146, 74, 12};
    static constexpr RectI kMechStatusDoneButtonRect{58, 176, 74, 12};
    static constexpr RectI kMechRepairDoneButtonRect{72, 161, 74, 12};
    static constexpr RectI kMechReloadPanelRect{15, 144, 150, 50};
    static constexpr RectI kMechReloadReloadButtonRect{70, 173, 52, 8};
    static constexpr RectI kMechReloadCancelButtonRect{70, 181, 52, 8};
    static constexpr RectI kMechSellOfferPanelRect{10, 114, 193, 76};
    static constexpr RectI kMechSellAcceptButtonRect{67, 165, 78, 8};
    static constexpr RectI kMechSellRejectButtonRect{67, 175, 78, 8};
    static constexpr int kMechBuyListPanelX = 92;
    static constexpr int kMechBuyListPanelY = 43;
    static constexpr int kMechBuyListPanelWidth = 136;
    static constexpr int kMechBuyListTitleY = 48;
    static constexpr int kMechBuyListFirstRowY = 60;
    static constexpr int kMechBuyListLineStep = 10;
    static constexpr int kMechBuyListMaxVisibleRows = 12;
    static constexpr int kMaxOwnedMechs = 12;
    static constexpr int kMechBuyMenuFirstY = 170;
    static constexpr int kMechBuyMenuLineStep = 8;
    static constexpr RectI kMechBuyMessagePanelRect{52, 82, 216, 38};
    static constexpr std::array<int, 4> kCrewPilotCellXs = {{45, 114, 183, 252}};
    static constexpr int kCrewTopRowY = 0;
    static constexpr int kCrewMechImageRowY = 132;
    static constexpr int kCrewCellWidth = 68;
    static constexpr int kCrewLabelX = 4;
    static constexpr int kCrewValueX = 49;
    static constexpr size_t kCrewDoneSelectionIndex = 4;
    static constexpr RectI kCrewDoneButtonRect{0, 118, 44, 12};
    static constexpr RectI kRecruitMainPanelRect{10, 10, 192, 116};
    static constexpr RectI kRecruitStatsPanelRect{148, 136, 152, 50};
    static constexpr RectI kRecruitNoCandidatesPanelRect{20, 83, 280, 35};
    static constexpr RectI kRecruitCrewFullPanelRect{84, 78, 152, 45};
    static constexpr RectI kRecruitYesButtonRect{93, 102, 46, 10};
    static constexpr RectI kRecruitNoButtonRect{100, 112, 36, 10};
    static constexpr std::string_view kFallbackStartingPlanetName = "OSHIKA";
    static constexpr uint64_t kTravelPilotCostPerJump = 2500;
    static constexpr uint64_t kTravelMechBaseCost = 20000;
    static constexpr uint64_t kTravelMechCostPerJump = 25000;
    static constexpr uint8_t kTravelMaxPilots = 4;
    static constexpr uint8_t kTravelMaxMechs = 12;
    static constexpr DWORD kTravelRoutePreviewMs = 1000;
    static constexpr int kTravelShuttleEntry = 63;
    static constexpr int kTravelEngineX = 53;
    static constexpr int kTravelEngineY = 141;
    static constexpr DWORD kTravelEngineDelayMs = 1000;
    static constexpr DWORD kTravelEngineFrameMs = 120;
    static constexpr DWORD kTravelEngineFinalHoldMs = 1000;
    static constexpr DWORD kContractAcceptedMessageMs = 2500;
    static constexpr int kMissionLaunchIconEntry = 8;
    static constexpr RectI kStarmapTravelButtonRect{245, 141, 60, 17};
    static constexpr RectI kStarmapPlanetsButtonRect{245, 160, 60, 17};
    static constexpr RectI kStarmapCancelButtonRect{245, 179, 60, 17};
    static constexpr RectI kStarmapPlanetNameInputRect{7, 5, 119, 13};
    static constexpr size_t kStarmapNoButtonSelection = std::numeric_limits<size_t>::max();
    static constexpr size_t kHouseMenuItemCount = 5;
    static constexpr size_t kStarmapPlanetNameInputMaxChars = 18;
    static constexpr RectI kNewsNetPreviousButtonRect{39, 181, 78, 16};
    static constexpr RectI kNewsNetNextButtonRect{122, 181, 76, 16};
    static constexpr RectI kNewsNetDoneButtonRect{205, 181, 76, 16};
    static constexpr RectI kContractMenuPanelRect{90, 150, 140, 38};
    static constexpr RectI kContractMenuRequestButtonRect{90, 160, 140, 10};
    static constexpr RectI kContractMenuLeaveButtonRect{90, 171, 140, 10};
    static constexpr RectI kContractPriceValueRect{176, 126, 43, 9};
    static constexpr RectI kContractSalvageValueRect{84, 134, 27, 9};
    static constexpr RectI kContractAdvanceValueRect{174, 142, 27, 9};
    static constexpr RectI kBattleStubWinButtonRect{82, 72, 156, 16};
    static constexpr RectI kBattleStubLoseButtonRect{82, 96, 156, 16};
    static constexpr RectI kBattleStubRunButtonRect{82, 120, 156, 16};
    static constexpr RectI kDeathPlayAgainRect{42, 103, 92, 10};
    static constexpr RectI kDeathQuitRect{42, 114, 52, 10};
    static constexpr int kMissionResultDeathImageEntry = 0;
    static constexpr int kMissionResultVictoryImageEntry = 1;
    static constexpr int kMissionResultDefeatImageEntry = 2;
    static constexpr RectI kMissionDebriefTopPanelRect{2, 2, 315, 137};
    static constexpr RectI kMissionDebriefBottomPanelRect{2, 142, 315, 56};
    static constexpr int kMissionDebriefEmblemX = 256;
    static constexpr int kMissionDebriefEmblemY = 143;
    static constexpr int kMissionDebriefMessageTextWidth = 244;
    static constexpr int kDebriefFrameUpperLeftEntry = 36;
    static constexpr int kDebriefFrameUpperRightEntry = 37;
    static constexpr int kDebriefFrameLowerLeftEntry = 38;
    static constexpr int kDebriefFrameLowerRightEntry = 39;
    static constexpr int kDebriefFrameTopEdgeEntry = 40;
    static constexpr int kDebriefFrameBottomEdgeEntry = 41;
    static constexpr int kDebriefFrameLeftEdgeEntry = 42;
    static constexpr int kDebriefFrameRightEdgeEntry = 43;
    static constexpr uint8_t kNewsNetTextColor = 2;
    static constexpr uint8_t kContractTextColor = 2;
    static constexpr uint8_t kContractVariableColor = 7;
    static constexpr uint8_t kContractValueColor = 4;
    static constexpr uint8_t kContractSelectedValueColor = 14;
    static constexpr int kContractMaxPriceK = 9990;
    static constexpr int kContractPriceStepK = 10;
    static constexpr std::array<ContractImageSpec, 5> kContractHouseNameImages = {{
        {0x000439A0u, 129, 36, 0},
        {0x000442CDu, 130, 36, 1},
        {0x00044BFBu, 129, 36, 0},
        {0x00045528u, 129, 36, 1},
        {0x00045E56u, 129, 36, 0},
    }};
    static constexpr std::array<ContractImageSpec, 5> kContractHouseEmblemImages = {{
        {0x000487EBu, 55, 49, 1},
        {0x00048D50u, 55, 49, 1},
        {0x000492B6u, 55, 49, 0},
        {0x0004981Bu, 55, 49, 0},
        {0x00049D80u, 55, 49, 0},
    }};
    static constexpr std::array<size_t, 5> kContractContactPortraitCounts = {{
        5,
        5,
        4,
        5,
        5,
    }};
    static constexpr std::array<std::array<ContractImageSpec, 5>, 5> kContractContactPortraitImages = {{
        {{
            {0x0004A2E5u, 90, 104, 0},
            {0x0004B536u, 90, 104, 0},
            {0x0004C787u, 90, 104, 0},
            {0x0004D9D8u, 90, 104, 1},
            {0x0004EC29u, 90, 104, 1},
        }},
        {{
            {0x0004FE7Bu, 90, 104, 0},
            {0x000510CCu, 90, 104, 0},
            {0x0005231Du, 90, 104, 1},
            {0x0005356Fu, 90, 104, 0},
            {0x000547C0u, 90, 104, 0},
        }},
        {{
            {0x00055A11u, 90, 104, 0},
            {0x00056C62u, 90, 104, 1},
            {0x00057EB3u, 90, 104, 1},
            {0x00059104u, 90, 104, 1},
            {},
        }},
        {{
            {0x0005A361u, 90, 104, 1},
            {0x0005B5B2u, 90, 104, 1},
            {0x0005C804u, 90, 104, 0},
            {0x0005DA55u, 90, 104, 0},
            {0x0005ECA6u, 90, 104, 0},
        }},
        {{
            {0x0005FEF7u, 90, 104, 0},
            {0x00061148u, 91, 104, 0},
            {0x00062401u, 90, 104, 1},
            {0x00063652u, 91, 104, 1},
            {0x000648DDu, 91, 104, 1},
        }},
    }};
    static constexpr std::array<ContractMissionDefinition, 34> kContractMissionDefinitions = {{
        {L"GARRISON DUTY", L"Defense", false},
        {L"GENERAL SECURITY DUTY", L"Defense", false},
        {L"DEFENSE OF A WATER FACTORY", L"Defense", false},
        {L"DEFENSE OF A WEAPONS FACTORY", L"Defense", false},
        {L"DEFENSE OF A FUEL DUMP", L"Defense", false},
        {L"DEFENSE OF FIELD COM UNIT", L"Defense", false},
        {L"DEFENSE OF A SUPPLY DEPOT", L"Defense", false},
        {L"DEFENSE OF LANDING FACILITIES", L"Defense", false},
        {L"SUPPRESSION OF REBELLION", L"Deathmatch", false},
        {L"TEMPORARY RELIEF OF FORCES", L"Sprint", false},
        {L"RESCUE OF A KIDNAP VICTIM", L"Retrieval", false},
        {L"RESCUE OF HOSTAGES", L"Retrieval", false},
        {L"RETRIEVAL OF STOLEN PROPERTY", L"Retrieval", false},
        {L"RETRIEVAL OF CAPTURED MECHS", L"Retrieval", false},
        {L"AN EXTENDED OFFENSIVE CAMPAIGN", L"Extended", true},
        {L"AN EXTENDED DEFENSIVE CAMPAIGN", L"Extended", true},
        {L"A PLANETARY ASSUALT", L"Deathmatch", false},
        {L"AN EXTENDED SIEGE CAMPAIGN", L"Extended", true},
        {L"RELIEF OF ENGAGED FORCES", L"Sprint", false},
        {L"A RECONNAISSANCE RAID", L"Deathmatch", false},
        {L"A DIVERSIONARY RAID", L"Deathmatch", false},
        {L"CONTAINMENT OF SECURITY FORCES", L"Sprint", false},
        {L"DESTRUCTION OF A WATER FACTORY", L"Assault", false},
        {L"DISABLING OF A WEAPONS FACTORY", L"Assault", false},
        {L"DESTRUCTION OF A FUEL DUMP", L"Assault", false},
        {L"DESTRUCTION OF AN AMMO DUMP", L"Assault", false},
        {L"DISABLING OF A FIELD COM CENTER", L"Assault", false},
        {L"ELIMINATION OF GARRISON FORCES", L"Deathmatch", false},
        {L"DESTROYING STOLEN PROTOTYPES", L"Assault", false},
        {L"DESTRUCTION OF MECH FACILITIES", L"Assault", false},
        {L"ELIMINATION OF SECURITY FORCES", L"Deathmatch", false},
        {L"DESTRUCTION OF PORT FACILITIES", L"Assault", false},
        {L"CAPTURE OF AMMO AND MECH PARTS", L"Retrieval", false},
        {L"PARTICIPATING IN HOSTAGE RAID", L"Retrieval", false},
    }};
    static constexpr MwMainTextRef kCampaignIntroText{
        "mw_main.endgame.020c12",
        0x020C12u,
        271u,
        7u,
    };
    static constexpr MwMainTextRef kNewsNetNoOtherText{
        "mw_main.company.00b264",
        0x00B264u,
        26u,
        1u,
    };
    static constexpr MwMainTextRef kMissionVictoryText{
        "mw_main.result.00a442",
        0x00A442u,
        67u,
        4u,
    };
    static constexpr MwMainTextRef kMissionDefeatText{
        "mw_main.result.00a29b",
        0x00A29Bu,
        144u,
        6u,
    };
    static constexpr MwMainTextRef kMissionDeathPromptText{
        "mw_main.story.014432",
        0x014432u,
        96u,
        5u,
    };
    static constexpr MwMainTextRef kStoryStartingBarClue1{
        "mw_main.rumor.012f40",
        0x012F40u,
        273u,
        7u,
    };
    static constexpr MwMainTextRef kStoryStartingBarClue2{
        "mw_main.rumor.013055",
        0x013055u,
        219u,
        6u,
    };
    static constexpr MwMainTextRef kStoryLandsEndSetup{
        "mw_main.rumor.013134",
        0x013134u,
        250u,
        7u,
    };
    static constexpr MwMainTextRef kStoryLandsEndContact{
        "mw_main.rumor.013232",
        0x013232u,
        425u,
        11u,
    };
    static constexpr MwMainTextRef kStoryOptionalCrestLore{
        "mw_main.rumor.0133f2",
        0x0133F2u,
        534u,
        14u,
    };
    static constexpr MwMainTextRef kStoryGrigEscort{
        "mw_main.rumor.01360c",
        0x01360Cu,
        562u,
        14u,
    };
    static constexpr MwMainTextRef kStoryGrigOffer{
        "mw_main.story.013842",
        0x013842u,
        503u,
        15u,
    };
    static constexpr MwMainTextRef kStoryGrigYes{
        "mw_main.story.013a3d",
        0x013A3Du,
        44u,
        2u,
    };
    static constexpr MwMainTextRef kStoryGrigNo{
        "mw_main.story.013a6d",
        0x013A6Du,
        177u,
        5u,
    };
    static constexpr MwMainTextRef kStoryGaledonLead{
        "mw_main.story.013b22",
        0x013B22u,
        277u,
        7u,
    };
    static constexpr MwMainTextRef kStoryDustballEntry{
        "mw_main.story.013c3b",
        0x013C3Bu,
        287u,
        8u,
    };
    static constexpr MwMainTextRef kStoryDustballPrompt{
        "mw_main.story.013d5e",
        0x013D5Eu,
        537u,
        16u,
    };
    static constexpr MwMainTextRef kStoryDustballFight{
        "mw_main.story.013f7c",
        0x013F7Cu,
        522u,
        16u,
    };
    static constexpr MwMainTextRef kStoryDustballRun{
        "mw_main.story.01418b",
        0x01418Bu,
        371u,
        12u,
    };
    static constexpr MwMainTextRef kStoryDustballDeath{
        "mw_main.story.014302",
        0x014302u,
        300u,
        8u,
    };
    static constexpr MwMainTextRef kStoryDustballRunRun{
        "mw_main.story.014496",
        0x014496u,
        696u,
        18u,
    };
    static constexpr MwMainTextRef kStoryDustballFightRun{
        "mw_main.story.014750",
        0x014750u,
        496u,
        14u,
    };
    static constexpr MwMainTextRef kStoryStoneArrowInquiry{
        "mw_main.story.014944",
        0x014944u,
        201u,
        5u,
    };
    static constexpr MwMainTextRef kStoryStoneArrowResult{
        "mw_main.story.014a11",
        0x014A11u,
        448u,
        12u,
    };
    static constexpr MwMainTextRef kStoryScorpionPilotIntro{
        "mw_main.story.014be5",
        0x014BE5u,
        364u,
        9u,
    };
    static constexpr MwMainTextRef kStoryScorpionPilotLead{
        "mw_main.story.014d52",
        0x014D52u,
        427u,
        13u,
    };
    static constexpr MwMainTextRef kStoryScorpionPilotLeadSuffix{
        "mw_main.story.014eff",
        0x014EFFu,
        40u,
        2u,
    };
    static constexpr MwMainTextRef kStorySniperPrompt{
        "mw_main.story.014f2c",
        0x014F2Cu,
        806u,
        23u,
    };
    static constexpr MwMainTextRef kStorySniperDeath{
        "mw_main.story.015256",
        0x015256u,
        301u,
        8u,
    };
    static constexpr MwMainTextRef kStorySniperRun{
        "mw_main.story.0153eb",
        0x0153EBu,
        443u,
        11u,
    };
    static constexpr MwMainTextRef kStoryKearneyBarLead{
        "mw_main.story.01574c",
        0x01574Cu,
        159u,
        4u,
    };
    static constexpr MwMainTextRef kStoryKearneyMeeting{
        "mw_main.story.0157ef",
        0x0157EFu,
        959u,
        25u,
    };
    static constexpr MwMainTextRef kStoryKearneyAddressPrompt{
        "mw_main.story.015bb3",
        0x015BB3u,
        103u,
        5u,
    };
    static constexpr MwMainTextRef kStoryKearneyOfficePrompt{
        "mw_main.story.015c1f",
        0x015C1Fu,
        688u,
        21u,
    };
    static constexpr MwMainTextRef kStoryKearneyForget{
        "mw_main.story.015ed3",
        0x015ED3u,
        66u,
        2u,
    };
    static constexpr MwMainTextRef kStoryOfficeHide{
        "mw_main.story.015f18",
        0x015F18u,
        285u,
        11u,
    };
    static constexpr MwMainTextRef kStoryOfficeFight{
        "mw_main.story.016039",
        0x016039u,
        649u,
        16u,
    };
    static constexpr MwMainTextRef kStoryOfficeTalk{
        "mw_main.story.0162c6",
        0x0162C6u,
        182u,
        5u,
    };
    static constexpr MwMainTextRef kStoryOfficeHideRun{
        "mw_main.story.0163e4",
        0x0163E4u,
        498u,
        12u,
    };
    static constexpr MwMainTextRef kStoryAlbieroRaidIntro{
        "mw_main.story.01663f",
        0x01663Fu,
        557u,
        14u,
    };
    static constexpr MwMainTextRef kStoryAlbieroMapLead{
        "mw_main.story.01686f",
        0x01686Fu,
        845u,
        21u,
    };
    static constexpr MwMainTextRef kStoryAlbieroLoading{
        "mw_main.story.016bbf",
        0x016BBFu,
        747u,
        19u,
    };
    static constexpr MwMainTextRef kStoryAlbieroFollowPrompt{
        "mw_main.story.016eae",
        0x016EAEu,
        602u,
        17u,
    };
    static constexpr MwMainTextRef kStoryAlbieroStayDown{
        "mw_main.story.01710c",
        0x01710Cu,
        706u,
        18u,
    };
    static constexpr MwMainTextRef kStoryTrustKearneyDeath{
        "mw_main.story.0173d2",
        0x0173D2u,
        474u,
        12u,
    };
    static constexpr MwMainTextRef kStoryTrustTashaSuccess{
        "mw_main.story.017614",
        0x017614u,
        659u,
        16u,
    };
    static constexpr MwMainTextRef kStoryTashaReward{
        "mw_main.story.0178aa",
        0x0178AAu,
        705u,
        17u,
    };
    static constexpr MwMainTextRef kStoryOperationInroadDisk{
        "mw_main.story.017b6c",
        0x017B6Cu,
        514u,
        14u,
    };
    static constexpr MwMainTextRef kStoryAlbieroCargoDoor{
        "mw_main.story.017d85",
        0x017D85u,
        705u,
        17u,
    };
    static constexpr MwMainTextRef kStoryAlbieroTrustPrompt{
        "mw_main.story.01804a",
        0x01804Au,
        892u,
        24u,
    };
    static constexpr MwMainTextRef kStoryQuietBarDrink{
        "mw_main.endgame.020aab",
        0x020AABu,
        53u,
        2u,
    };
    static constexpr MwMainTextRef kStoryFinalBasePrompt{
        "mw_main.endgame.020ae5",
        0x020AE5u,
        110u,
        6u,
    };
    static constexpr MwMainTextRef kStoryAndersMoonArrest{
        "mw_main.endgame.020d25",
        0x020D25u,
        277u,
        7u,
    };
    static constexpr MwMainTextRef kStoryNewsTashaFiles{
        "mw_main.pm.0183ca",
        0x0183CAu,
        376u,
        15u,
    };
    static constexpr MwMainTextRef kStoryNewsMatabushiProposal{
        "mw_main.pm.018545",
        0x018545u,
        406u,
        11u,
    };
    static constexpr MwMainTextRef kStoryNewsMatabushiDarkWingOption{
        "mw_main.pm.0186de",
        0x0186DEu,
        345u,
        12u,
    };
    static constexpr MwMainTextRef kStoryNewsMatabushiPreparations{
        "mw_main.pm.01883a",
        0x01883Au,
        496u,
        15u,
    };
    static constexpr MwMainTextRef kStoryNewsMatabushiProceed{
        "mw_main.pm.018a2b",
        0x018A2Bu,
        219u,
        9u,
    };
    static constexpr MwMainTextRef kStoryNewsJordanAlbieroLead{
        "mw_main.pm.018b08",
        0x018B08u,
        320u,
        9u,
    };
    static constexpr MwMainTextRef kStoryBlackWidowAccept{
        "mw_main.news.018f57",
        0x018F57u,
        168u,
        5u,
    };
    static constexpr MwMainTextRef kStoryBlackWidowFight{
        "mw_main.news.019003",
        0x019003u,
        736u,
        19u,
    };
    static constexpr MwMainTextRef kStoryBlackWidowStandoff{
        "mw_main.news.0192e6",
        0x0192E6u,
        567u,
        14u,
    };
    static constexpr MwMainTextRef kStoryTashaIntro{
        "mw_main.news.019520",
        0x019520u,
        817u,
        21u,
    };
    static constexpr MwMainTextRef kStoryTashaReveal{
        "mw_main.news.019854",
        0x019854u,
        435u,
        11u,
    };
    static constexpr MwMainTextRef kStoryBlackWidowBar{
        "mw_main.news.019a0c",
        0x019A0Cu,
        944u,
        25u,
    };
    static constexpr MwMainTextRef kStoryNewsMarikAmbush{
        "mw_main.story.0155ac",
        0x0155ACu,
        412u,
        13u,
    };
    static constexpr MwMainTextRef kStoryNewsKangarooJackDeath{
        "mw_main.news.018c4e",
        0x018C4Eu,
        773u,
        21u,
    };
    static constexpr MwMainTextRef kStoryNewsBlackWidowLead{
        "mw_main.news.019dc0",
        0x019DC0u,
        205u,
        7u,
    };
    static constexpr MwMainTextRef kStoryNewsBlackWidowLeadSuffix{
        "mw_main.news.019e8f",
        0x019E8Fu,
        145u,
        5u,
    };

    struct StoryNewsNetEntry {
        MwMainTextRef text;
        uint8_t prerequisiteMessageId = 0;
        uint8_t messageId = 0;
    };

    struct NewsNetEntry {
        MwMainTextRef text;
        int year = 0;
        int month = 0;
        int day = 0;
        uint8_t messageId = 0;
    };

    static constexpr std::array<NewsNetEntry, 51> kNewsNetEntries = {{
        {{"mw_main.news.01c0b4", 0x01C0B4u, 587u, 17u}, 3024, 4, 1, 0x01},
        {{"mw_main.news.01c418", 0x01C418u, 494u, 15u}, 3024, 4, 8, 0x02},
        {{"mw_main.news.01c303", 0x01C303u, 273u, 8u}, 3024, 4, 15, 0x05},
        {{"mw_main.news.01c60a", 0x01C60Au, 760u, 21u}, 3024, 5, 1, 0x04},
        {{"mw_main.news.019f24", 0x019F24u, 618u, 18u}, 3024, 6, 30, 0x2C},
        {{"mw_main.news.01ad7d", 0x01AD7Du, 596u, 18u}, 3024, 7, 1, 0x1D},
        {{"mw_main.news.01a192", 0x01A192u, 374u, 12u}, 3024, 8, 25, 0x2A},
        {{"mw_main.news.01beee", 0x01BEEEu, 450u, 14u}, 3024, 11, 15, 0x10},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3025, 4, 8, 0x03},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3026, 4, 8, 0x03},
        {{"mw_main.news.01afd5", 0x01AFD5u, 571u, 17u}, 3026, 6, 1, 0x16},
        {{"mw_main.news.01ac1a", 0x01AC1Au, 351u, 12u}, 3026, 8, 15, 0x1E},
        {{"mw_main.news.01a9f0", 0x01A9F0u, 550u, 17u}, 3026, 9, 5, 0x1F},
        {{"mw_main.news.01a84d", 0x01A84Du, 415u, 14u}, 3026, 11, 30, 0x20},
        {{"mw_main.news.01b3c6", 0x01B3C6u, 669u, 19u}, 3027, 3, 15, 0x14},
        {{"mw_main.news.01a6a6", 0x01A6A6u, 421u, 14u}, 3027, 7, 15, 0x21},
        {{"mw_main.news.01a4fb", 0x01A4FBu, 423u, 15u}, 3027, 8, 17, 0x22},
        {{"mw_main.news.01a30c", 0x01A30Cu, 491u, 15u}, 3027, 11, 15, 0x23},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3027, 4, 8, 0x03},
        {{"mw_main.news.01b214", 0x01B214u, 430u, 14u}, 3027, 5, 15, 0x15},
        {{"mw_main.news.01b971", 0x01B971u, 698u, 20u}, 3028, 1, 2, 0x11},
        {{"mw_main.news.01bc2f", 0x01BC2Fu, 699u, 20u}, 3028, 1, 3, 0x12},
        {{"mw_main.news.01b667", 0x01B667u, 774u, 21u}, 3028, 1, 14, 0x13},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3028, 4, 8, 0x03},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3029, 4, 8, 0x03},
        {{"mw_main.news.01e383", 0x01E383u, 737u, 20u}, 3025, 1, 22, 0x55},
        {{"mw_main.news.01e668", 0x01E668u, 647u, 18u}, 3024, 11, 5, 0x56},
        {{"mw_main.news.01e8f3", 0x01E8F3u, 421u, 13u}, 3025, 3, 5, 0x57},
        {{"mw_main.news.01ea9c", 0x01EA9Cu, 652u, 18u}, 3025, 4, 10, 0x58},
        {{"mw_main.news.01ed2c", 0x01ED2Cu, 751u, 21u}, 3025, 9, 15, 0x59},
        {{"mw_main.news.01f01f", 0x01F01Fu, 623u, 17u}, 3025, 2, 14, 0x5A},
        {{"mw_main.news.01f292", 0x01F292u, 687u, 19u}, 3026, 8, 28, 0x5C},
        {{"mw_main.news.01f545", 0x01F545u, 741u, 20u}, 3028, 1, 12, 0x5D},
        {{"mw_main.headline.01f82e", 0x01F82Eu, 170u, 5u}, 3027, 28, 1, 0x5E},
        {{"mw_main.headline.01f8dc", 0x01F8DCu, 141u, 4u}, 3027, 10, 2, 0x5F},
        {{"mw_main.headline.01f96d", 0x01F96Du, 141u, 4u}, 3027, 31, 3, 0x60},
        {{"mw_main.headline.01f9fe", 0x01F9FEu, 139u, 4u}, 3027, 30, 4, 0x61},
        {{"mw_main.headline.01fa8d", 0x01FA8Du, 141u, 4u}, 3027, 6, 8, 0x62},
        {{"mw_main.headline.01fb1e", 0x01FB1Eu, 293u, 11u}, 3027, 10, 31, 0x63},
        {{"mw_main.headline.01fc47", 0x01FC47u, 175u, 5u}, 3027, 11, 28, 0x70},
        {{"mw_main.headline.01fcfa", 0x01FCFAu, 133u, 4u}, 3028, 6, 30, 0x65},
        {{"mw_main.headline.01fd83", 0x01FD83u, 289u, 10u}, 3028, 8, 13, 0x66},
        {{"mw_main.headline.01fea8", 0x01FEA8u, 268u, 11u}, 3028, 8, 22, 0x67},
        {{"mw_main.headline.01ffb8", 0x01FFB8u, 406u, 14u}, 3028, 8, 30, 0x68},
        {{"mw_main.headline.020152", 0x020152u, 124u, 4u}, 3028, 10, 11, 0x69},
        {{"mw_main.headline.0201d2", 0x0201D2u, 338u, 11u}, 3028, 10, 28, 0x6A},
        {{"mw_main.headline.020328", 0x020328u, 120u, 4u}, 3028, 11, 23, 0x6B},
        {{"mw_main.headline.0203a4", 0x0203A4u, 365u, 14u}, 3029, 1, 15, 0x6C},
        {{"mw_main.news.020515", 0x020515u, 406u, 11u}, 3029, 4, 15, 0x6D},
        {{"mw_main.news.0206af", 0x0206AFu, 607u, 16u}, 3029, 4, 30, 0x6E},
        {{"mw_main.news.020912", 0x020912u, 405u, 11u}, 3029, 5, 15, 0x6F},
    }};

    static constexpr std::array<StoryNewsNetEntry, 9> kStoryNewsNetEntries = {{
        {kStoryNewsMarikAmbush, 0x29, 0x2B},
        {kStoryNewsKangarooJackDeath, 0x2E, 0x3A},
        {kStoryNewsBlackWidowLead, 0x2E, 0x36},
        {kStoryNewsTashaFiles, 0x39, 0x3B},
        {kStoryNewsMatabushiProposal, 0x39, 0x3C},
        {kStoryNewsMatabushiDarkWingOption, 0x39, 0x3D},
        {kStoryNewsMatabushiPreparations, 0x39, 0x3E},
        {kStoryNewsMatabushiProceed, 0x39, 0x3F},
        {kStoryNewsJordanAlbieroLead, 0x38, 0x40},
    }};

    static constexpr std::array<PilotTextDefinition, 42> kRecruitPilotDefinitions = {{
        {"elli_gujar", { "mw_main.pilot_name.011e7a", 0x011E7Au, 10u, 1u }, { "mw_main.pilot_quote.011e85", 0x011E85u, 52u, 3u }}, // portrait=1 gunnery=0 piloting=0 ELLI GUJAR
        {"karen_blak", { "mw_main.pilot_name.011ebd", 0x011EBDu, 10u, 1u }, { "mw_main.pilot_quote.011ec8", 0x011EC8u, 52u, 3u }}, // portrait=1 gunnery=1 piloting=0 KAREN BLAK
        {"taris_ren", { "mw_main.pilot_name.011f00", 0x011F00u, 10u, 1u }, { "mw_main.pilot_quote.011f0b", 0x011F0Bu, 45u, 3u }}, // portrait=2 gunnery=0 piloting=0 TARIS REN
        {"tj", { "mw_main.pilot_name.011f3c", 0x011F3Cu, 10u, 1u }, { "mw_main.pilot_quote.011f47", 0x011F47u, 49u, 3u }}, // portrait=2 gunnery=1 piloting=0 TJ
        {"d_sajak", { "mw_main.pilot_name.011f7c", 0x011F7Cu, 10u, 1u }, { "mw_main.pilot_quote.011f87", 0x011F87u, 47u, 3u }}, // portrait=3 gunnery=0 piloting=1 D. SAJAK
        {"pam_north", { "mw_main.pilot_name.011fba", 0x011FBAu, 10u, 1u }, { "mw_main.pilot_quote.011fc5", 0x011FC5u, 45u, 3u }}, // portrait=3 gunnery=1 piloting=0 PAM NORTH
        {"tara_ellis", { "mw_main.pilot_name.011ff6", 0x011FF6u, 10u, 1u }, { "mw_main.pilot_quote.012001", 0x012001u, 58u, 3u }}, // portrait=4 gunnery=1 piloting=2 TARA ELLIS
        {"a_morris", { "mw_main.pilot_name.01203f", 0x01203Fu, 10u, 1u }, { "mw_main.pilot_quote.01204a", 0x01204Au, 56u, 4u }}, // portrait=4 gunnery=2 piloting=1 A. MORRIS
        {"ilsa_marn", { "mw_main.pilot_name.012086", 0x012086u, 10u, 1u }, { "mw_main.pilot_quote.012091", 0x012091u, 61u, 4u }}, // portrait=5 gunnery=1 piloting=2 ILSA MARN
        {"ara", { "mw_main.pilot_name.0120d2", 0x0120D2u, 10u, 1u }, { "mw_main.pilot_quote.0120dd", 0x0120DDu, 59u, 4u }}, // portrait=5 gunnery=2 piloting=1 ARA
        {"s_miller", { "mw_main.pilot_name.01211c", 0x01211Cu, 10u, 1u }, { "mw_main.pilot_quote.012127", 0x012127u, 47u, 3u }}, // portrait=6 gunnery=1 piloting=2 S. MILLER
        {"rose_artz", { "mw_main.pilot_name.01215a", 0x01215Au, 10u, 1u }, { "mw_main.pilot_quote.012165", 0x012165u, 55u, 4u }}, // portrait=6 gunnery=2 piloting=1 ROSE ARTZ
        {"jj_kitter", { "mw_main.pilot_name.0121a0", 0x0121A0u, 10u, 1u }, { "mw_main.pilot_quote.0121ab", 0x0121ABu, 62u, 4u }}, // portrait=7 gunnery=2 piloting=3 JJ KITTER
        {"emma_meyer", { "mw_main.pilot_name.0121ed", 0x0121EDu, 10u, 1u }, { "mw_main.pilot_quote.0121f8", 0x0121F8u, 55u, 3u }}, // portrait=7 gunnery=3 piloting=2 EMMA MEYER
        {"erin_falls", { "mw_main.pilot_name.012233", 0x012233u, 10u, 1u }, { "mw_main.pilot_quote.01223e", 0x01223Eu, 36u, 3u }}, // portrait=8 gunnery=2 piloting=3 ERIN FALLS
        {"a_chu_lai", { "mw_main.pilot_name.012266", 0x012266u, 10u, 1u }, { "mw_main.pilot_quote.012271", 0x012271u, 60u, 4u }}, // portrait=8 gunnery=3 piloting=3 A. CHU LAI
        {"zera_tith", { "mw_main.pilot_name.0122b1", 0x0122B1u, 10u, 1u }, { "mw_main.pilot_quote.0122bc", 0x0122BCu, 64u, 4u }}, // portrait=9 gunnery=3 piloting=2 ZERA TITH
        {"chaney_ti", { "mw_main.pilot_name.012300", 0x012300u, 10u, 1u }, { "mw_main.pilot_quote.01230b", 0x01230Bu, 41u, 3u }}, // portrait=9 gunnery=3 piloting=3 CHANEY TI
        {"bob_true", { "mw_main.pilot_name.012338", 0x012338u, 10u, 1u }, { "mw_main.pilot_quote.012343", 0x012343u, 61u, 4u }}, // portrait=10 gunnery=0 piloting=0 BOB TRUE
        {"ted_hegel", { "mw_main.pilot_name.012384", 0x012384u, 10u, 1u }, { "mw_main.pilot_quote.01238f", 0x01238Fu, 52u, 3u }}, // portrait=10 gunnery=1 piloting=0 TED HEGEL
        {"tim_seers", { "mw_main.pilot_name.0123c7", 0x0123C7u, 10u, 1u }, { "mw_main.pilot_quote.0123d2", 0x0123D2u, 40u, 3u }}, // portrait=11 gunnery=0 piloting=0 TIM SEERS
        {"z_rodgers", { "mw_main.pilot_name.0123fe", 0x0123FEu, 10u, 1u }, { "mw_main.pilot_quote.012409", 0x012409u, 49u, 3u }}, // portrait=11 gunnery=0 piloting=1 Z. RODGERS
        {"td_bente", { "mw_main.pilot_name.01243e", 0x01243Eu, 10u, 1u }, { "mw_main.pilot_quote.012449", 0x012449u, 44u, 3u }}, // portrait=12 gunnery=0 piloting=1 T.D. BENTE
        {"t_rich", { "mw_main.pilot_name.012479", 0x012479u, 10u, 1u }, { "mw_main.pilot_quote.012484", 0x012484u, 52u, 3u }}, // portrait=12 gunnery=1 piloting=0 T. RICH
        {"john_zoe", { "mw_main.pilot_name.0124bc", 0x0124BCu, 10u, 1u }, { "mw_main.pilot_quote.0124c7", 0x0124C7u, 37u, 2u }}, // portrait=13 gunnery=1 piloting=1 JOHN ZOE
        {"kelly_t", { "mw_main.pilot_name.0124f0", 0x0124F0u, 10u, 1u }, { "mw_main.pilot_quote.0124fb", 0x0124FBu, 43u, 3u }}, // portrait=13 gunnery=1 piloting=2 KELLY T.
        {"kinji_tsi", { "mw_main.pilot_name.01252a", 0x01252Au, 10u, 1u }, { "mw_main.pilot_quote.012535", 0x012535u, 41u, 3u }}, // portrait=14 gunnery=2 piloting=1 KINJI TSI
        {"billy_chow", { "mw_main.pilot_name.012562", 0x012562u, 10u, 1u }, { "mw_main.pilot_quote.01256d", 0x01256Du, 45u, 3u }}, // portrait=14 gunnery=1 piloting=1 BILLY CHOW
        {"alex_veer", { "mw_main.pilot_name.01259e", 0x01259Eu, 10u, 1u }, { "mw_main.pilot_quote.0125a9", 0x0125A9u, 45u, 3u }}, // portrait=15 gunnery=1 piloting=2 ALEX VEER
        {"pete_ryan", { "mw_main.pilot_name.0125da", 0x0125DAu, 10u, 1u }, { "mw_main.pilot_quote.0125e5", 0x0125E5u, 55u, 3u }}, // portrait=15 gunnery=2 piloting=1 PETE RYAN
        {"b_hendrik", { "mw_main.pilot_name.012620", 0x012620u, 10u, 1u }, { "mw_main.pilot_quote.01262b", 0x01262Bu, 54u, 4u }}, // portrait=16 gunnery=1 piloting=2 B. HENDRIK
        {"a_cooper", { "mw_main.pilot_name.012665", 0x012665u, 10u, 1u }, { "mw_main.pilot_quote.012670", 0x012670u, 57u, 4u }}, // portrait=16 gunnery=2 piloting=1 A. COOPER
        {"tricky_nik", { "mw_main.pilot_name.0126ad", 0x0126ADu, 10u, 1u }, { "mw_main.pilot_quote.0126b8", 0x0126B8u, 54u, 3u }}, // portrait=17 gunnery=2 piloting=2 TRICKY NIK
        {"killer", { "mw_main.pilot_name.0126f2", 0x0126F2u, 10u, 1u }, { "mw_main.pilot_quote.0126fd", 0x0126FDu, 62u, 4u }}, // portrait=17 gunnery=2 piloting=2 KILLER
        {"r_torsak", { "mw_main.pilot_name.01273f", 0x01273Fu, 10u, 1u }, { "mw_main.pilot_quote.01274a", 0x01274Au, 47u, 3u }}, // portrait=18 gunnery=2 piloting=3 R. TORSAK
        {"g_lofall", { "mw_main.pilot_name.01277d", 0x01277Du, 10u, 1u }, { "mw_main.pilot_quote.012788", 0x012788u, 52u, 3u }}, // portrait=18 gunnery=3 piloting=2 G. LOFALL
        {"darc_horse", { "mw_main.pilot_name.0127c0", 0x0127C0u, 10u, 1u }, { "mw_main.pilot_quote.0127cb", 0x0127CBu, 49u, 3u }}, // portrait=19 gunnery=2 piloting=3 DARC HORSE
        {"jim_eagle", { "mw_main.pilot_name.012800", 0x012800u, 10u, 1u }, { "mw_main.pilot_quote.01280b", 0x01280Bu, 48u, 3u }}, // portrait=19 gunnery=3 piloting=2 JIM EAGLE
        {"tank_smith", { "mw_main.pilot_name.01283f", 0x01283Fu, 10u, 1u }, { "mw_main.pilot_quote.01284a", 0x01284Au, 52u, 3u }}, // portrait=20 gunnery=2 piloting=3 TANK SMITH
        {"kevin_call", { "mw_main.pilot_name.012882", 0x012882u, 10u, 1u }, { "mw_main.pilot_quote.01288d", 0x01288Du, 50u, 3u }}, // portrait=20 gunnery=2 piloting=3 KEVIN CALL
        {"j_holmaas", { "mw_main.pilot_name.0128c3", 0x0128C3u, 10u, 1u }, { "mw_main.pilot_quote.0128ce", 0x0128CEu, 43u, 3u }}, // portrait=21 gunnery=3 piloting=3 J. HOLMAAS
        {"nasty_bill", { "mw_main.pilot_name.0128fd", 0x0128FDu, 10u, 1u }, { "mw_main.pilot_quote.012908", 0x012908u, 48u, 3u }}, // portrait=21 gunnery=3 piloting=3 NASTY BILL
    }};

    static constexpr std::array<int, 4> kMechLabWeldEntries = {{
        31,
        32,
        33,
        34,
    }};

    static constexpr std::array<DWORD, 4> kMechLabWeldDurations = {{
        1200,
        1500,
        1800,
        1350,
    }};

    static constexpr std::array<int, 14> kTravelEngineSequence = {{
        1,
        2,
        1,
        2,
        3,
        2,
        3,
        4,
        3,
        4,
        5,
        4,
        5,
        6,
    }};

    static constexpr std::array<RectI, 6> kPlanetIconRects = {{
        {25, 57, 38, 44},
        {25, 106, 38, 44},
        {25, 156, 38, 44},
        {257, 57, 38, 44},
        {257, 106, 38, 44},
        {257, 156, 38, 44},
    }};

    static constexpr std::array<MenuItem, 3> kBarMenuItems = {{
        {L"ORDER DRINK", MenuAction::None},
        {L"RECRUIT CREW", MenuAction::RecruitCrew},
        {L"LEAVE", MenuAction::Continue},
    }};

    static constexpr std::array<MenuItem, 4> kMechLabMenuItems = {{
        {L"REVIEW MECHS", MenuAction::ReviewMechs},
        {L"EXTRA AMMO", MenuAction::ExtraAmmo},
        {L"BUY MECHS", MenuAction::BuyMechs},
        {L"DONE", MenuAction::Continue},
    }};

    static constexpr std::array<AmmoDefinition, 6> kAmmoDefinitions = {{
        {L"AC 5-PKS", 285, {L"SHADOW HAWK", L"WOLVERINE", L"RIFLEMAN", L"MARAUDER"}, 4},
        {L"LRM 5-PKS", 1475, {L"SHADOW HAWK", L"", L"", L""}, 1},
        {L"SRM 2-PKS", 637, {L"WASP", L"SHADOW HAWK", L"", L""}, 2},
        {L"SRM 4-PKS", 1274, {L"JENNER", L"", L"", L""}, 1},
        {L"SRM 6-PKS", 2124, {L"WOLVERINE", L"WARHAMMER", L"BATTLEMASTER", L""}, 3},
        {L"MACH GUN", 5, {L"LOCUST", L"PHOENIX HAWK", L"WARHAMMER", L"BATTLEMASTER"}, 4},
    }};

    static constexpr std::array<MenuItem, 3> kStatusMenuItems = {{
        {L"CREW", MenuAction::Crew},
        {L"NEWS NET", MenuAction::NewsNet},
        {L"DONE", MenuAction::Continue},
    }};

    static constexpr std::array<MenuItem, 7> kSystemMenuItems = {{
        {L"SAVE GAME", MenuAction::SaveGame},
        {L"RESTORE GAME", MenuAction::RestoreGame},
        {L"TURN SOUND OFF", MenuAction::ToggleSound},
        {L"DETAIL: LOW", MenuAction::Detail},
        {L"RESTART GAME", MenuAction::Restart},
        {L"EXIT TO DOS", MenuAction::ExitToDos},
        {L"CONTINUE", MenuAction::Continue},
    }};

    static constexpr std::array<size_t, kArmorSectionCount> kArmorDamageOrder = {{
        0, 1, 2, 3, 4, 5, 6, 7, 8,
    }};

    static constexpr std::array<MechDefinition, kPlayableMechCount> kMechDefinitions = {{
        {
            ChassisId::Locust,
            L"LOCUST",
            {15, 0x0003F115u, 68, 68, 0},
            {7, 0x0002A00Du, 111, 182, 1},
            20, 129, 0, 10, 0,
            {{{L"M LAS", L"CT", DamageState::Functional}, {L"MG", L"RA", DamageState::Functional}, {L"MG", L"LA", DamageState::Functional}}},
            {{1504000, 1804000, 1955000, 2256000}},
            {{1353000, 1654000, 1804000, 2105000}},
            30,
        },
        {
            ChassisId::Wasp,
            L"WASP",
            {15, 0x0003F115u, 68, 68, 0},
            {7, 0x0002A00Du, 111, 182, 1},
            20, 95, 180, 10, 6,
            {{{L"M LAS", L"RA", DamageState::Functional}, {L"SRM2", L"LT", DamageState::Functional}}},
            {{1504000, 1804000, 1955000, 2256000}},
            {{1353000, 1654000, 1804000, 2105000}},
            0,
        },
        {
            ChassisId::Jenner,
            L"JENNER",
            {16, 0x0003FA26u, 68, 68, 1},
            {8, 0x0002C7E6u, 125, 165, 1},
            35, 118, 150, 10, 3,
            {{{L"SRM4", L"CT", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}, {L"M LAS", L"LA", DamageState::Functional}, {L"M LAS", L"LA", DamageState::Functional}}},
            {{3183000, 3819000, 4137000, 4774000}},
            {{2864000, 3501000, 3819000, 4456000}},
            24,
        },
        {
            ChassisId::PhoenixHawk,
            L"PHOENIX HAWK",
            {17, 0x00040338u, 68, 68, 0},
            {9, 0x0002F08Au, 115, 183, 1},
            45, 97, 180, 10, 6,
            {{{L"L LAS", L"RA", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}, {L"M LAS", L"LA", DamageState::Functional}, {L"MG", L"LA", DamageState::Functional}, {L"MG", L"RA", DamageState::Functional}}},
            {{4022000, 4826000, 5228000, 6033000}},
            {{3619000, 4424000, 4826000, 5630000}},
            16,
        },
        {
            ChassisId::ShadowHawk,
            L"SHADOW HAWK",
            {18, 0x00040C49u, 68, 68, 1},
            {10, 0x00031A09u, 119, 182, 1},
            55, 86, 90, 12, 3,
            {{{L"AC/5", L"LT", DamageState::Functional}, {L"LRM5", L"RT", DamageState::Functional}, {L"SRM2", L"HD", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}}},
            {{4622000, 5546000, 6008000, 6933000}},
            {{4159000, 5084000, 5546000, 6470000}},
            13,
        },
        {
            ChassisId::Wolverine,
            L"WOLVERINE",
            {18, 0x00040C49u, 68, 68, 1},
            {10, 0x00031A09u, 119, 182, 1},
            55, 86, 150, 12, 5,
            {{{L"AC/5", L"RA", DamageState::Functional}, {L"SRM6", L"LT", DamageState::Functional}, {L"M LAS", L"HD", DamageState::Functional}}},
            {{4622000, 5546000, 6008000, 6933000}},
            {{4159000, 5084000, 5546000, 6470000}},
            0,
        },
        {
            ChassisId::Rifleman,
            L"RIFLEMAN",
            {19, 0x0004155Bu, 68, 68, 0},
            {11, 0x000344BBu, 135, 187, 0},
            60, 64, 0, 10, 0,
            {{{L"L LAS", L"RA", DamageState::Functional}, {L"L LAS", L"LA", DamageState::Functional}, {L"AC/5", L"RA", DamageState::Functional}, {L"AC/5", L"LA", DamageState::Functional}, {L"M LAS", L"RT", DamageState::Functional}, {L"M LAS", L"LT", DamageState::Functional}}},
            {{5500000, 6600000, 7150000, 8250000}},
            {{4950000, 6050000, 6600000, 7700000}},
            9,
        },
        {
            ChassisId::Warhammer,
            L"WARHAMMER",
            {20, 0x00041E6Cu, 68, 68, 1},
            {12, 0x0003766Fu, 113, 178, 1},
            70, 64, 0, 18, 0,
            {{{L"PPC", L"RA", DamageState::Functional}, {L"PPC", L"LA", DamageState::Functional}, {L"SRM6", L"RT", DamageState::Functional}, {L"M LAS", L"RT", DamageState::Functional}, {L"M LAS", L"LT", DamageState::Functional}, {L"S LAS", L"RT", DamageState::Functional}, {L"S LAS", L"LT", DamageState::Functional}, {L"MG", L"RT", DamageState::Functional}, {L"MG", L"LT", DamageState::Functional}}},
            {{6021000, 7225000, 7827000, 9031000}},
            {{5418000, 6623000, 7225000, 8429000}},
            6,
        },
        {
            ChassisId::Marauder,
            L"MARAUDER",
            {21, 0x0004277Eu, 68, 68, 0},
            {13, 0x00039E1Au, 121, 161, 1},
            75, 64, 0, 16, 0,
            {{{L"PPC", L"RA", DamageState::Functional}, {L"PPC", L"LA", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}, {L"M LAS", L"LA", DamageState::Functional}, {L"AC/5", L"RT", DamageState::Functional}}},
            {{6729000, 8074000, 8747000, 10093000}},
            {{6056000, 7401000, 8074000, 9420000}},
            4,
        },
        {
            ChassisId::Battlemaster,
            L"BATTLEMASTER",
            {22, 0x0004308Fu, 68, 68, 0},
            {14, 0x0003C480u, 125, 181, 1},
            85, 64, 0, 18, 0,
            {{{L"PPC", L"RA", DamageState::Functional}, {L"M LAS", L"RT", DamageState::Functional}, {L"M LAS", L"RT", DamageState::Functional}, {L"M LAS", L"RT", DamageState::Functional}, {L"MG", L"LA", DamageState::Functional}, {L"MG", L"LA", DamageState::Functional}, {L"SRM6", L"LT", DamageState::Functional}, {L"M LAS", L"LT", DamageState::Functional}, {L"M LAS", L"LT", DamageState::Functional}, {L"M LAS", L"LT", DamageState::Functional}}},
            {{8410000, 10092000, 10933000, 12615000}},
            {{7569000, 9251000, 10092000, 11774000}},
            2,
        },
    }};

    static constexpr size_t chassisIndex(ChassisId chassis) {
        return static_cast<size_t>(chassis);
    }

    static const MechDefinition& mechDefinition(ChassisId chassis) {
        return kMechDefinitions[chassisIndex(chassis)];
    }

    static int ammoIndexForWeapon(std::wstring_view weapon) {
        if (weapon == L"AC/5") {
            return 0;
        }
        if (weapon == L"LRM5") {
            return 1;
        }
        if (weapon == L"SRM2") {
            return 2;
        }
        if (weapon == L"SRM4") {
            return 3;
        }
        if (weapon == L"SRM6") {
            return 4;
        }
        if (weapon == L"MG") {
            return 5;
        }
        return -1;
    }

    static std::array<bool, 6> ammoTypesForMech(const OwnedMech& mech) {
        std::array<bool, 6> used = {};
        for (const MechWeaponStatus& weapon : mech.weapons) {
            if (weapon.weapon.empty()) {
                continue;
            }
            const int ammoIndex = ammoIndexForWeapon(weapon.weapon);
            if (ammoIndex >= 0 && static_cast<size_t>(ammoIndex) < used.size()) {
                used[static_cast<size_t>(ammoIndex)] = true;
            }
        }
        return used;
    }

    static OwnedMech makeMech(ChassisId chassis, int assignedCrewSlot = -1) {
        const MechDefinition& definition = mechDefinition(chassis);
        OwnedMech mech;
        mech.chassis = chassis;
        mech.name = definition.name;
        mech.imageEntry = definition.crewImage.entry;
        mech.assignedCrewSlot = assignedCrewSlot;
        mech.statusImageEntry = definition.statusImage.entry;
        mech.condition = DamageState::Functional;
        mech.repairCost = 0;
        mech.tons = definition.tons;
        mech.speedKph = definition.speedKph;
        mech.jumpCapMeters = definition.jumpCapMeters;
        mech.heatSinksWorking = definition.heatSinks;
        mech.heatSinksTotal = definition.heatSinks;
        mech.jumpJetsWorking = definition.jumpJets;
        mech.jumpJetsTotal = definition.jumpJets;
        mech.armorPercent = 100;
        mech.armorDamage = {};
        mech.weapons = definition.weapons;
        const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
        for (size_t i = 0; i < ammoTypes.size(); ++i) {
            if (ammoTypes[i]) {
                mech.ammoPacks[i] = kMechAmmoMaxPacks;
            }
        }
        return mech;
    }

    static OwnedMech makeStartingJenner(int assignedCrewSlot = 0) {
        OwnedMech mech = makeMech(ChassisId::Jenner, assignedCrewSlot);
        mech.ammoPacks[3] = 8;
        return mech;
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (app) {
            return app->handleMessage(hwnd, message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_TIMER:
            update();
            render();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            if (handleDebugKeyDown(wParam, lParam)) {
                return 0;
            }
#endif
            handleKey(wParam);
            return 0;
        case WM_CHAR:
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            if (handleDebugChar(wParam)) {
                return 0;
            }
#endif
            handleChar(wParam);
            return 0;
        case WM_LBUTTONDOWN:
            handleMouseClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
            return 0;
        case WM_RBUTTONDOWN:
            handleMouseClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
            return 0;
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
    void debugLog(std::wstring_view message) {
        debugTools_.log(message);
    }

    static std::wstring_view screenStateName(ScreenState state) {
        switch (state) {
        case ScreenState::ActivisionSplash:
            return L"ActivisionSplash";
        case ScreenState::IntroText:
            return L"IntroText";
        case ScreenState::Title:
            return L"Title";
        case ScreenState::Authorization:
            return L"Authorization";
        case ScreenState::CampaignMessage:
            return L"CampaignMessage";
        case ScreenState::MainMenu:
            return L"MainMenu";
        case ScreenState::StatusMenu:
            return L"StatusMenu";
        case ScreenState::NewsNet:
            return L"NewsNet";
        case ScreenState::ContractMenu:
            return L"ContractMenu";
        case ScreenState::ContractNegotiation:
            return L"ContractNegotiation";
        case ScreenState::ContractAcceptedMessage:
            return L"ContractAcceptedMessage";
        case ScreenState::MechLabMenu:
            return L"MechLabMenu";
        case ScreenState::MechExtraAmmo:
            return L"MechExtraAmmo";
        case ScreenState::MechReviewList:
            return L"MechReviewList";
        case ScreenState::MechStatus:
            return L"MechStatus";
        case ScreenState::MechRepairStatus:
            return L"MechRepairStatus";
        case ScreenState::MechReloadPrompt:
            return L"MechReloadPrompt";
        case ScreenState::MechSellOffer:
            return L"MechSellOffer";
        case ScreenState::MechBuyList:
            return L"MechBuyList";
        case ScreenState::MechBuyStatus:
            return L"MechBuyStatus";
        case ScreenState::MechBuyDamageStatus:
            return L"MechBuyDamageStatus";
        case ScreenState::MechBuyCannotAfford:
            return L"MechBuyCannotAfford";
        case ScreenState::MechBuyTooMany:
            return L"MechBuyTooMany";
        case ScreenState::BarMenu:
            return L"BarMenu";
        case ScreenState::SystemMenu:
            return L"SystemMenu";
        case ScreenState::SaveGameNameInput:
            return L"SaveGameNameInput";
        case ScreenState::RestoreGameList:
            return L"RestoreGameList";
        case ScreenState::CrewMenu:
            return L"CrewMenu";
        case ScreenState::Starmap:
            return L"Starmap";
        case ScreenState::TravelRoutePreview:
            return L"TravelRoutePreview";
        case ScreenState::TravelAnimation:
            return L"TravelAnimation";
        case ScreenState::MissionBattleStub:
            return L"MissionBattleStub";
        case ScreenState::MissionDebrief:
            return L"MissionDebrief";
        }
        return L"Unknown";
    }

    bool handleDebugKeyDown(WPARAM key, LPARAM lParam) {
        if (key == VK_OEM_3) {
            const bool wasDown = (lParam & (1l << 30)) != 0;
            if (!wasDown) {
                debugConsoleOpen_ = !debugConsoleOpen_;
                suppressNextDebugConsoleChar_ = true;
                debugLog(debugConsoleOpen_ ? L"Debug console opened." : L"Debug console closed.");
            }
            return true;
        }

        if (!debugConsoleOpen_) {
            return false;
        }

        if (key == VK_RETURN) {
            executeDebugConsoleCommand();
        } else if (key == VK_BACK) {
            if (!debugConsoleInput_.empty()) {
                debugConsoleInput_.pop_back();
            }
        } else if (key == VK_ESCAPE) {
            debugConsoleOpen_ = false;
            debugLog(L"Debug console closed.");
        }
        return true;
    }

    bool handleDebugChar(WPARAM character) {
        if (!debugConsoleOpen_) {
            return false;
        }

        const wchar_t ch = static_cast<wchar_t>(character);
        if (suppressNextDebugConsoleChar_) {
            suppressNextDebugConsoleChar_ = false;
            if (ch == L'`' || ch == L'~' || ch == 0x0451 || ch == 0x0401) {
                return true;
            }
        }

        if (ch == L'\r' || ch == L'\n' || ch == L'\b' || ch == 27) {
            return true;
        }
        if (ch >= 32 && debugConsoleInput_.size() < kDebugConsoleMaxInput) {
            debugConsoleInput_.push_back(ch);
        }
        return true;
    }

    static std::string normalizeDebugCommand(std::wstring_view text) {
        size_t begin = 0;
        while (begin < text.size() && text[begin] <= L' ') {
            ++begin;
        }
        size_t end = text.size();
        while (end > begin && text[end - 1] <= L' ') {
            --end;
        }

        std::string result;
        result.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
            const wchar_t ch = text[i];
            if (ch < 128) {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
        }
        return result;
    }

    void executeDebugConsoleCommand() {
        const std::wstring entered = debugConsoleInput_;
        debugConsoleInput_.clear();

        const std::string command = normalizeDebugCommand(entered);
        if (command.empty()) {
            return;
        }

        debugLog(L"> " + widen(command));

        const std::array<CheatDefinition, 19> cheats = {{
            {"money", &App::cheatMoney},
            {"exp1", &App::cheatPilotSkillPoor},
            {"exp2", &App::cheatPilotSkillAverage},
            {"exp3", &App::cheatPilotSkillGood},
            {"exp4", &App::cheatPilotSkillExcellent},
            {"mech_all", &App::cheatAddAllMechs},
            {"mech_locust", &App::cheatAddLocust},
            {"mech_jenner", &App::cheatAddJenner},
            {"mech_phoenixhawk", &App::cheatAddPhoenixHawk},
            {"mech_shadowhawk", &App::cheatAddShadowHawk},
            {"mech_rifleman", &App::cheatAddRifleman},
            {"mech_warhammer", &App::cheatAddWarhammer},
            {"mech_marauder", &App::cheatAddMarauder},
            {"mech_battlemaster", &App::cheatAddBattlemaster},
            {"rep1", &App::cheatReputationRisky},
            {"rep2", &App::cheatReputationWorthWatching},
            {"rep3", &App::cheatReputationVeteran},
            {"rep4", &App::cheatReputationElite},
            {"month", &App::cheatNextMonth},
        }};

        for (const CheatDefinition& cheat : cheats) {
            if (command == cheat.name) {
                (this->*cheat.execute)();
                return;
            }
        }

        debugLog(L"Unknown debug command: " + widen(command));
    }
#endif

    void handleMouseClick(int clientX, int clientY, bool rightButton) {
        if (rightButton) {
            if (state_ == ScreenState::BarMenu && barDialogState_ != BarDialogState::None) {
                closeBarDialog();
                return;
            }
            if (state_ == ScreenState::CrewMenu && crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                closeCrewAssignment();
                return;
            }
            if (state_ == ScreenState::StatusMenu ||
                state_ == ScreenState::NewsNet ||
                state_ == ScreenState::ContractMenu ||
                state_ == ScreenState::ContractNegotiation ||
                state_ == ScreenState::MechLabMenu ||
                state_ == ScreenState::MechExtraAmmo ||
                state_ == ScreenState::MechReviewList ||
                state_ == ScreenState::MechStatus ||
                state_ == ScreenState::MechRepairStatus ||
                state_ == ScreenState::MechReloadPrompt ||
                state_ == ScreenState::MechSellOffer ||
                state_ == ScreenState::MechBuyList ||
                state_ == ScreenState::MechBuyStatus ||
                state_ == ScreenState::MechBuyDamageStatus ||
                state_ == ScreenState::MechBuyCannotAfford ||
                state_ == ScreenState::MechBuyTooMany ||
                state_ == ScreenState::BarMenu ||
                state_ == ScreenState::SystemMenu ||
                state_ == ScreenState::SaveGameNameInput ||
                state_ == ScreenState::RestoreGameList ||
                state_ == ScreenState::CrewMenu ||
                state_ == ScreenState::Starmap ||
                state_ == ScreenState::MissionDebrief) {
                if (state_ == ScreenState::CrewMenu) {
                    changeState(ScreenState::StatusMenu);
                } else if (state_ == ScreenState::SaveGameNameInput) {
                    systemMenuIndex_ = kSystemSaveMenuIndex;
                    changeState(ScreenState::SystemMenu);
                } else if (state_ == ScreenState::RestoreGameList) {
                    systemMenuIndex_ = kSystemRestoreMenuIndex;
                    changeState(ScreenState::SystemMenu);
                } else if (state_ == ScreenState::MechExtraAmmo) {
                    changeState(ScreenState::MechLabMenu);
                } else if (state_ == ScreenState::MechRepairStatus ||
                           state_ == ScreenState::MechReloadPrompt ||
                           state_ == ScreenState::MechSellOffer) {
                    changeState(ScreenState::MechStatus);
                } else if (state_ == ScreenState::MechBuyCannotAfford ||
                           state_ == ScreenState::MechBuyTooMany) {
                    changeState(ScreenState::MechBuyStatus);
                } else if (state_ == ScreenState::MechBuyDamageStatus) {
                    changeState(ScreenState::MechBuyStatus);
                } else if (state_ == ScreenState::MechBuyStatus) {
                    changeState(ScreenState::MechBuyList);
                } else if (state_ == ScreenState::MechBuyList) {
                    changeState(ScreenState::MechLabMenu);
                } else if (state_ == ScreenState::MechReviewList ||
                           state_ == ScreenState::MechStatus) {
                    changeState(ScreenState::MechLabMenu);
                } else if (state_ == ScreenState::MissionDebrief && missionDebriefOutcome_ == MissionOutcome::Death) {
                    return;
                } else if (state_ == ScreenState::StatusMenu ||
                           state_ == ScreenState::ContractMenu ||
                           state_ == ScreenState::MechLabMenu ||
                           state_ == ScreenState::BarMenu ||
                           state_ == ScreenState::SystemMenu ||
                           state_ == ScreenState::Starmap) {
                    returnToMainMenu();
                } else {
                    changeState(ScreenState::MainMenu);
                }
            }
            return;
        }

        if (advanceStartupScreen()) {
            return;
        }

        int screenX = 0;
        int screenY = 0;
        if (!clientToScreenPixel(clientX, clientY, screenX, screenY)) {
            return;
        }

        if (state_ == ScreenState::CampaignMessage && isStorySceneActive()) {
            handleStoryClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::Starmap) {
            handleStarmapClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::NewsNet) {
            handleNewsNetClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::ContractMenu) {
            handleContractMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::ContractNegotiation) {
            handleContractNegotiationClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::StatusMenu) {
            handleStatusMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::CrewMenu) {
            handleCrewMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechLabMenu) {
            handleMechLabMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechExtraAmmo) {
            handleExtraAmmoClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechReviewList) {
            handleMechReviewClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechStatus) {
            handleMechStatusClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechRepairStatus) {
            handleMechRepairClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechReloadPrompt) {
            handleMechReloadClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechSellOffer) {
            handleMechSellOfferClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechBuyList) {
            handleMechBuyListClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechBuyStatus) {
            handleMechBuyStatusClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechBuyDamageStatus) {
            handleMechBuyDamageClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MechBuyCannotAfford || state_ == ScreenState::MechBuyTooMany) {
            changeState(ScreenState::MechBuyStatus);
            return;
        }

        if (state_ == ScreenState::BarMenu) {
            handleBarMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::SystemMenu) {
            handleSystemMenuClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::RestoreGameList) {
            handleRestoreGameClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MissionBattleStub) {
            handleBattleStubClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::MissionDebrief) {
            handleMissionDebriefClick(screenX, screenY);
            return;
        }

        if (state_ != ScreenState::MainMenu) {
            return;
        }

        const int iconIndex = hitPlanetIcon(screenX, screenY);
        if (iconIndex < 0) {
            return;
        }
        if (!planetIconAvailable(static_cast<size_t>(iconIndex))) {
            return;
        }
        planetMenuIndex_ = static_cast<size_t>(iconIndex);
        activatePlanetIcon(planetMenuIndex_);
    }

    void activatePlanetIcon(size_t iconIndex) {
        if (!planetIconAvailable(iconIndex)) {
            return;
        }
        if (iconIndex == kPlanetStatusIconIndex) {
            statusMenuIndex_ = 0;
            changeState(ScreenState::StatusMenu);
        } else if (iconIndex == kPlanetMechLabIconIndex) {
            mechLabMenuIndex_ = 0;
            if (tryStartMechBayStory()) {
                return;
            }
            changeState(ScreenState::MechLabMenu);
        } else if (iconIndex == kPlanetContractIconIndex) {
            if (contractAccepted_) {
                beginMissionLaunch();
                return;
            }
            contractMenuIndex_ = 0;
            if (tryStartContractStory()) {
                return;
            }
            changeState(ScreenState::ContractMenu);
        } else if (contractAccepted_) {
            return;
        } else if (iconIndex == kPlanetStarmapIconIndex) {
            if (!planets_.empty()) {
                selectedPlanetIndex_ = std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
            }
            starmapButtonIndex_ = kStarmapNoButtonSelection;
            closeStarmapMenu();
            changeState(ScreenState::Starmap);
        } else if (iconIndex == kPlanetBarIconIndex) {
            if (tryStartBarEntryStory()) {
                return;
            }
            changeState(ScreenState::BarMenu);
        } else if (iconIndex == kPlanetSystemIconIndex) {
            systemMenuIndex_ = kSystemSaveMenuIndex;
            changeState(ScreenState::SystemMenu);
        }
    }

    bool clientToScreenPixel(int clientX, int clientY, int& screenX, int& screenY) const {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientW = client.right - client.left;
        const int clientH = client.bottom - client.top;
        const RectI displayRect = displayRectForClient(clientW, clientH);
        if (clientX < displayRect.x ||
            clientY < displayRect.y ||
            clientX >= displayRect.x + displayRect.width ||
            clientY >= displayRect.y + displayRect.height) {
            return false;
        }

        screenX = (clientX - displayRect.x) * kScreenWidth / displayRect.width;
        screenY = (clientY - displayRect.y) * kScreenHeight / displayRect.height;
        return screenX >= 0 && screenX < kScreenWidth && screenY >= 0 && screenY < kScreenHeight;
    }

    int hitPlanetIcon(int screenX, int screenY) const {
        for (size_t i = 0; i < kPlanetIconRects.size(); ++i) {
            const RectI& rect = kPlanetIconRects[i];
            if (screenX >= rect.x &&
                screenX < rect.x + rect.width &&
                screenY >= rect.y &&
                screenY < rect.y + rect.height) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool hitRect(const RectI& rect, int screenX, int screenY) const {
        return screenX >= rect.x &&
            screenX < rect.x + rect.width &&
            screenY >= rect.y &&
            screenY < rect.y + rect.height;
    }

    int hitMenuItem(int screenX, int screenY, int x, int y, int width, size_t count, int lineStep = 10) const {
        if (screenX < x || screenX >= x + width || screenY < y) {
            return -1;
        }

        const int relativeY = screenY - y;
        const int itemIndex = relativeY / lineStep;
        if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= count) {
            return -1;
        }
        return itemIndex;
    }

    void handleStatusMenuClick(int screenX, int screenY) {
        const int itemIndex = hitMenuItem(screenX, screenY, 158, 153, 142, kStatusMenuItems.size());
        if (itemIndex < 0) {
            return;
        }

        statusMenuIndex_ = static_cast<size_t>(itemIndex);
        activateStatusMenuItem(statusMenuIndex_);
    }

    void handleMechLabMenuClick(int screenX, int screenY) {
        const int itemIndex = hitMenuItem(screenX, screenY, 90, 147, 140, kMechLabMenuItems.size());
        if (itemIndex < 0) {
            return;
        }

        mechLabMenuIndex_ = static_cast<size_t>(itemIndex);
        activateMechLabMenuItem(mechLabMenuIndex_);
    }

    void handleExtraAmmoClick(int screenX, int screenY) {
        if (screenX >= kExtraAmmoMainPanelRect.x &&
            screenX < kExtraAmmoMainPanelRect.x + kExtraAmmoMainPanelRect.width) {
            for (size_t i = 0; i < kAmmoDefinitions.size(); ++i) {
                const int rowY = kExtraAmmoFirstRowY + static_cast<int>(i) * kExtraAmmoLineStep;
                if (screenY >= rowY && screenY < rowY + kExtraAmmoLineStep) {
                    const bool wasSelected = extraAmmoSelectionIndex_ == i;
                    extraAmmoSelectionIndex_ = i;
                    if (wasSelected) {
                        buySelectedExtraAmmo();
                    }
                    return;
                }
            }

            if (screenY >= kExtraAmmoDoneY && screenY < kExtraAmmoDoneY + 8) {
                extraAmmoSelectionIndex_ = kAmmoDefinitions.size();
                activateExtraAmmoSelection();
            }
        }
    }

    void handleMechReviewClick(int screenX, int screenY) {
        const RectI panel = mechReviewPanelRect();
        if (!hitRect(panel, screenX, screenY)) {
            return;
        }

        for (size_t i = 0; i < ownedMechs_.size(); ++i) {
            const int itemTop = kMechReviewItemY + static_cast<int>(i) * kMechReviewLineStep;
            if (screenY >= itemTop && screenY < itemTop + kMechReviewLineStep) {
                selectedMechIndex_ = i;
                activateMechReviewSelection();
                return;
            }
        }

        const int doneTop = kMechReviewItemY + static_cast<int>(ownedMechs_.size()) * kMechReviewLineStep;
        if (screenY >= doneTop && screenY < doneTop + kMechReviewLineStep) {
            selectedMechIndex_ = ownedMechs_.size();
            activateMechReviewSelection();
        }
    }

    void handleMechStatusClick(int screenX, int screenY) {
        if (screenX >= 50 && screenX < 140 && screenY >= 150 && screenY < 190) {
            mechStatusMenuIndex_ = static_cast<size_t>((screenY - 150) / 8);
            if (mechStatusMenuIndex_ < 5) {
                activateMechStatusSelection();
            }
        }
    }

    void handleMechRepairClick(int screenX, int screenY) {
        if (hitRect(kMechRepairDoneButtonRect, screenX, screenY)) {
            activateMechRepairSelection();
            return;
        }

        OwnedMech* mech = selectedOwnedMech();
        if (!mech || screenX >= kMechStatusTextAreaWidth) {
            return;
        }

        RepairTarget clicked;
        if (!repairTargetAtScreenY(*mech, screenY, clicked)) {
            return;
        }

        const std::vector<RepairTarget> targets = repairableTargets(*mech);
        for (size_t i = 0; i < targets.size(); ++i) {
            if (!sameRepairTarget(targets[i], clicked)) {
                continue;
            }
            const bool wasSelected = i == repairSelectionIndex_;
            repairSelectionIndex_ = i;
            if (wasSelected) {
                repairSelectedMechTarget();
            }
            return;
        }
    }

    void handleMechReloadClick(int screenX, int screenY) {
        if (hitRect(kMechReloadReloadButtonRect, screenX, screenY)) {
            reloadSelectionIndex_ = 0;
            activateMechReloadSelection();
        } else if (hitRect(kMechReloadCancelButtonRect, screenX, screenY)) {
            reloadSelectionIndex_ = 1;
            activateMechReloadSelection();
        }
    }

    void handleMechSellOfferClick(int screenX, int screenY) {
        if (hitRect(kMechSellAcceptButtonRect, screenX, screenY)) {
            sellOfferSelectionIndex_ = 0;
            activateMechSellOfferSelection();
        } else if (hitRect(kMechSellRejectButtonRect, screenX, screenY)) {
            sellOfferSelectionIndex_ = 1;
            activateMechSellOfferSelection();
        }
    }

    void handleMechBuyListClick(int screenX, int screenY) {
        const RectI panel = mechBuyListPanelRect();
        if (!hitRect(panel, screenX, screenY)) {
            return;
        }

        const PlanetMechMarket& market = currentPlanetMechMarket();
        const size_t count = market.mechsForSale.size();
        if (count == 0) {
            selectedMarketMechIndex_ = 0;
            activateMechBuyListSelection();
            return;
        }
        for (size_t row = 0; row < visibleMarketRows(count); ++row) {
            const int itemTop = kMechBuyListFirstRowY + static_cast<int>(row) * kMechBuyListLineStep;
            if (screenY >= itemTop && screenY < itemTop + kMechBuyListLineStep) {
                selectedMarketMechIndex_ = std::min(mechMarketScrollOffset_ + row, count - 1u);
                activateMechBuyListSelection();
                return;
            }
        }

        const int doneY = mechBuyListDoneY(count);
        if (screenY >= doneY && screenY < doneY + kMechBuyListLineStep) {
            selectedMarketMechIndex_ = count;
            activateMechBuyListSelection();
        }
    }

    void handleMechBuyStatusClick(int screenX, int screenY) {
        if (screenX < 50 || screenX >= 170 ||
            screenY < kMechBuyMenuFirstY ||
            screenY >= kMechBuyMenuFirstY + 3 * kMechBuyMenuLineStep) {
            return;
        }

        mechBuyMenuIndex_ = static_cast<size_t>((screenY - kMechBuyMenuFirstY) / kMechBuyMenuLineStep);
        if (mechBuyMenuIndex_ < 3) {
            activateMechBuyStatusSelection();
        }
    }

    void handleMechBuyDamageClick(int screenX, int screenY) {
        (void)screenX;
        if (screenY >= kMechRepairDoneY && screenY < kMechRepairDoneY + kMechBuyMenuLineStep) {
            changeState(ScreenState::MechBuyStatus);
        }
    }

    void handleBarMenuClick(int screenX, int screenY) {
        if (barDialogState_ != BarDialogState::None) {
            handleBarDialogClick(screenX, screenY);
            return;
        }

        const int itemIndex = hitMenuItem(screenX, screenY, 166, 150, 128, kBarMenuItems.size());
        if (itemIndex < 0) {
            return;
        }

        barMenuIndex_ = static_cast<size_t>(itemIndex);
        activateBarMenuItem(barMenuIndex_);
    }

    void handleBarDialogClick(int screenX, int screenY) {
        if (barDialogState_ == BarDialogState::RecruitOffer) {
            if (hitRect(kRecruitYesButtonRect, screenX, screenY)) {
                recruitChoiceIndex_ = 0;
                activateRecruitChoice();
            } else if (hitRect(kRecruitNoButtonRect, screenX, screenY)) {
                recruitChoiceIndex_ = 1;
                activateRecruitChoice();
            }
            return;
        }

        closeBarDialog();
    }

    void handleSystemMenuClick(int screenX, int screenY) {
        const int itemIndex = hitMenuItem(screenX, screenY, 90, 90, 140, kSystemMenuItems.size());
        if (itemIndex < 0) {
            return;
        }

        systemMenuIndex_ = static_cast<size_t>(itemIndex);
        activateSystemMenuItem(systemMenuIndex_);
    }

    void handleStarmapClick(int screenX, int screenY) {
        if (starmapNameInputActive_) {
            if (!hitRect(kStarmapPlanetNameInputRect, screenX, screenY)) {
                finishStarmapNameInput();
            }
            return;
        }
        if (starmapMenuMode_ != StarmapMenuMode::None) {
            handleStarmapMenuClick(screenX, screenY);
            return;
        }
        if (hitRect(kStarmapPlanetNameInputRect, screenX, screenY)) {
            beginStarmapNameInput();
            return;
        }
        if (hitRect(kStarmapCancelButtonRect, screenX, screenY)) {
            returnToMainMenu();
            return;
        }
        if (hitRect(kStarmapTravelButtonRect, screenX, screenY)) {
            beginTravelToSelectedPlanet();
            return;
        }
        if (hitRect(kStarmapPlanetsButtonRect, screenX, screenY)) {
            openStarmapHouseMenu();
            return;
        }

        const int planetIndex = hitStarmapPlanet(screenX, screenY);
        if (planetIndex >= 0) {
            selectedPlanetIndex_ = planetIndex;
        }
    }

    void handleStarmapMenuClick(int screenX, int screenY) {
        if (starmapMenuMode_ == StarmapMenuMode::Houses) {
            const int itemIndex = starmapHouseMenuItemAt(screenX, screenY);
            if (itemIndex < 0) {
                return;
            }
            starmapHouseSelectionIndex_ = static_cast<size_t>(itemIndex);
            activateStarmapHouseSelection();
            return;
        }

        if (starmapMenuMode_ == StarmapMenuMode::HousePlanets) {
            const std::vector<int> indexes = starmapPlanetIndexesForSelectedHouse();
            const int itemIndex = starmapPlanetMenuItemAt(screenX, screenY, indexes.size());
            if (itemIndex < 0) {
                return;
            }
            starmapPlanetSelectionIndex_ = static_cast<size_t>(itemIndex);
            activateStarmapPlanetSelection();
        }
    }

    void openStarmapHouseMenu() {
        starmapMenuMode_ = StarmapMenuMode::Houses;
        starmapHouseSelectionIndex_ = 0;
        starmapPlanetSelectionIndex_ = 0;
    }

    void closeStarmapMenu() {
        starmapMenuMode_ = StarmapMenuMode::None;
        starmapHouseSelectionIndex_ = 0;
        starmapPlanetSelectionIndex_ = 0;
    }

    void beginStarmapNameInput() {
        if (planets_.empty()) {
            return;
        }
        closeStarmapMenu();
        starmapButtonIndex_ = kStarmapNoButtonSelection;
        starmapNameInputActive_ = true;
        starmapNameInputOriginalPlanetIndex_ = std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        starmapNameInput_.clear();
    }

    void cancelStarmapNameInput() {
        if (starmapNameInputActive_) {
            selectedPlanetIndex_ = starmapNameInputOriginalPlanetIndex_;
        }
        starmapNameInputActive_ = false;
        starmapNameInput_.clear();
    }

    void finishStarmapNameInput() {
        if (!starmapNameInputActive_) {
            return;
        }

        const int planetIndex = findPlanetIndexByNameInsensitive(starmapNameInput_);
        if (planetIndex >= 0) {
            selectedPlanetIndex_ = planetIndex;
        } else {
            selectedPlanetIndex_ = starmapNameInputOriginalPlanetIndex_;
        }
        starmapNameInputActive_ = false;
        starmapNameInput_.clear();
    }

    void activateStarmapHouseSelection() {
        if (starmapHouseSelectionIndex_ >= kHouseMenuItemCount) {
            closeStarmapMenu();
            return;
        }

        starmapMenuMode_ = StarmapMenuMode::HousePlanets;
        starmapPlanetSelectionIndex_ = 0;
    }

    void activateStarmapPlanetSelection() {
        const std::vector<int> indexes = starmapPlanetIndexesForSelectedHouse();
        if (starmapPlanetSelectionIndex_ >= indexes.size()) {
            openStarmapHouseMenu();
            return;
        }

        selectedPlanetIndex_ = indexes[starmapPlanetSelectionIndex_];
        closeStarmapMenu();
    }

    void handleStarmapKey(WPARAM key) {
        if (starmapNameInputActive_) {
            if (key == VK_RETURN) {
                finishStarmapNameInput();
            } else if (key == VK_BACK) {
                if (!starmapNameInput_.empty()) {
                    starmapNameInput_.pop_back();
                }
            }
            return;
        }

        if (starmapMenuMode_ == StarmapMenuMode::Houses) {
            const size_t count = kHouseMenuItemCount + 1u;
            if (key == VK_UP || key == VK_LEFT) {
                starmapHouseSelectionIndex_ = (starmapHouseSelectionIndex_ + count - 1u) % count;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                starmapHouseSelectionIndex_ = (starmapHouseSelectionIndex_ + 1u) % count;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateStarmapHouseSelection();
            } else if (key == VK_ESCAPE) {
                closeStarmapMenu();
            }
            return;
        }

        if (starmapMenuMode_ == StarmapMenuMode::HousePlanets) {
            const std::vector<int> indexes = starmapPlanetIndexesForSelectedHouse();
            const size_t count = indexes.size() + 1u;
            if (count == 0) {
                openStarmapHouseMenu();
                return;
            }

            constexpr size_t kRowsPerColumn = 16;
            if (key == VK_UP) {
                starmapPlanetSelectionIndex_ = (starmapPlanetSelectionIndex_ + count - 1u) % count;
            } else if (key == VK_DOWN || key == VK_TAB) {
                starmapPlanetSelectionIndex_ = (starmapPlanetSelectionIndex_ + 1u) % count;
            } else if (key == VK_LEFT && starmapPlanetSelectionIndex_ < indexes.size()) {
                if (starmapPlanetSelectionIndex_ >= kRowsPerColumn) {
                    starmapPlanetSelectionIndex_ -= kRowsPerColumn;
                }
            } else if (key == VK_RIGHT && starmapPlanetSelectionIndex_ < indexes.size()) {
                const size_t target = starmapPlanetSelectionIndex_ + kRowsPerColumn;
                if (target < indexes.size()) {
                    starmapPlanetSelectionIndex_ = target;
                }
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateStarmapPlanetSelection();
            } else if (key == VK_ESCAPE) {
                openStarmapHouseMenu();
            }
            return;
        }

        if (key == VK_UP || key == VK_LEFT) {
            starmapButtonIndex_ = starmapButtonIndex_ == kStarmapNoButtonSelection
                ? 2u
                : (starmapButtonIndex_ + 2u) % 3u;
        } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
            starmapButtonIndex_ = starmapButtonIndex_ == kStarmapNoButtonSelection
                ? 0u
                : (starmapButtonIndex_ + 1u) % 3u;
        } else if (key == VK_RETURN || key == VK_SPACE) {
            activateStarmapButton();
        }
    }

    void activateStarmapButton() {
        if (starmapButtonIndex_ == kStarmapNoButtonSelection) {
            return;
        }
        if (starmapButtonIndex_ == 0) {
            beginTravelToSelectedPlanet();
        } else if (starmapButtonIndex_ == 1) {
            openStarmapHouseMenu();
        } else {
            returnToMainMenu();
        }
    }

    int starmapHouseMenuItemAt(int screenX, int screenY) const {
        const RectI rect = starmapHouseMenuRect();
        constexpr int kFirstY = 90;
        constexpr int kLineStep = 8;
        if (screenX < rect.x || screenX >= rect.x + rect.width || screenY < kFirstY) {
            return -1;
        }
        const int itemIndex = (screenY - kFirstY) / kLineStep;
        if (itemIndex < 0 || static_cast<size_t>(itemIndex) > kHouseMenuItemCount) {
            return -1;
        }
        return itemIndex;
    }

    RectI starmapHouseMenuRect() const {
        return {126, 72, 68, 72};
    }

    RectI starmapPlanetMenuRect() const {
        return {51, 31, 218, 165};
    }

    int starmapPlanetMenuItemAt(int screenX, int screenY, size_t planetCount) const {
        const RectI rect = starmapPlanetMenuRect();
        constexpr int kLeftX = 65;
        constexpr int kRightX = 172;
        constexpr int kFirstY = 55;
        constexpr int kLineStep = 8;
        constexpr int kRowsPerColumn = 16;
        if (screenY >= 185 && screenY < 193 && screenX >= rect.x && screenX < rect.x + rect.width) {
            return static_cast<int>(planetCount);
        }
        if (screenY < kFirstY || screenY >= kFirstY + kRowsPerColumn * kLineStep) {
            return -1;
        }
        int column = -1;
        if (screenX >= kLeftX && screenX < kLeftX + 96) {
            column = 0;
        } else if (screenX >= kRightX && screenX < kRightX + 96) {
            column = 1;
        }
        if (column < 0) {
            return -1;
        }

        const int row = (screenY - kFirstY) / kLineStep;
        const int itemIndex = row + column * kRowsPerColumn;
        if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= planetCount) {
            return -1;
        }
        return itemIndex;
    }

    std::vector<int> starmapPlanetIndexesForSelectedHouse() const {
        std::vector<int> indexes;
        const uint8_t houseId = static_cast<uint8_t>(std::min<size_t>(starmapHouseSelectionIndex_, kHouseMenuItemCount - 1u));
        for (size_t i = 0; i < planets_.size(); ++i) {
            if (planets_[i].houseId == houseId) {
                indexes.push_back(static_cast<int>(i));
            }
        }
        std::sort(
            indexes.begin(),
            indexes.end(),
            [this](int left, int right) {
                const PlanetRecord& leftPlanet = planets_[static_cast<size_t>(left)];
                const PlanetRecord& rightPlanet = planets_[static_cast<size_t>(right)];
                return leftPlanet.name < rightPlanet.name;
            });
        return indexes;
    }

    void handleNewsNetClick(int screenX, int screenY) {
        if (hitRect(kNewsNetPreviousButtonRect, screenX, screenY)) {
            newsNetButtonIndex_ = 0;
            activateNewsNetButton(newsNetButtonIndex_);
        } else if (hitRect(kNewsNetNextButtonRect, screenX, screenY)) {
            newsNetButtonIndex_ = 1;
            activateNewsNetButton(newsNetButtonIndex_);
        } else if (hitRect(kNewsNetDoneButtonRect, screenX, screenY)) {
            newsNetButtonIndex_ = 2;
            activateNewsNetButton(newsNetButtonIndex_);
        }
    }

    void handleContractMenuClick(int screenX, int screenY) {
        if (hitRect(kContractMenuRequestButtonRect, screenX, screenY)) {
            contractMenuIndex_ = 0;
            activateContractMenuItem(contractMenuIndex_);
        } else if (hitRect(kContractMenuLeaveButtonRect, screenX, screenY)) {
            contractMenuIndex_ = 1;
            activateContractMenuItem(contractMenuIndex_);
        }
    }

    void activateContractMenuItem(size_t itemIndex) {
        if (itemIndex == 0) {
            if (tryStartContractStory()) {
                return;
            }
            openContractNegotiation();
            return;
        }
        returnToMainMenu();
    }

    void handleContractNegotiationClick(int screenX, int screenY) {
        if (contractNegotiationTerminated_ || contractNegotiationUnavailable_) {
            changeState(ScreenState::ContractMenu);
            return;
        }

        if (hitRect(kContractPriceValueRect, screenX, screenY)) {
            beginContractTermEdit(ContractEditableField::Price);
            return;
        }
        if (hitRect(kContractSalvageValueRect, screenX, screenY)) {
            beginContractTermEdit(ContractEditableField::Salvage);
            return;
        }
        if (hitRect(kContractAdvanceValueRect, screenX, screenY)) {
            beginContractTermEdit(ContractEditableField::Advance);
            return;
        }

        contractEditableField_ = ContractEditableField::None;
        if (hitRect(kNewsNetPreviousButtonRect, screenX, screenY)) {
            contractNegotiationButtonIndex_ = 0;
            activateContractNegotiationButton(contractNegotiationButtonIndex_);
        } else if (hitRect(kNewsNetNextButtonRect, screenX, screenY)) {
            contractNegotiationButtonIndex_ = 1;
            activateContractNegotiationButton(contractNegotiationButtonIndex_);
        } else if (hitRect(kNewsNetDoneButtonRect, screenX, screenY)) {
            contractNegotiationButtonIndex_ = 2;
            activateContractNegotiationButton(contractNegotiationButtonIndex_);
        }
    }

    void activateContractNegotiationButton(size_t buttonIndex) {
        if (buttonIndex == 1) {
            if (activeContractIndex_ + 1u < activeContracts_.size()) {
                ++activeContractIndex_;
                contractEditableField_ = ContractEditableField::None;
                contractNegotiationButtonIndex_ = 1;
            } else {
                changeState(ScreenState::ContractMenu);
            }
            return;
        }
        if (buttonIndex == 2) {
            changeState(ScreenState::ContractMenu);
            return;
        }

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugLog(activeContractOffer() && activeContractOffer()->termsModified
            ? L"Contract submit selected."
            : L"Contract accept selected. Accept flow is not implemented yet.");
#endif
        ContractOffer* offer = activeContractOffer();
        if (!offer) {
            return;
        }
        if (offer->termsModified) {
            submitContractCounterOffer();
        } else {
            acceptActiveContract();
        }
    }

    void openContractNegotiation() {
        contractNegotiationTerminated_ = false;
        contractNegotiationUnavailable_ = false;
        activeContractIndex_ = 0;
        contractEditableField_ = ContractEditableField::None;
        contractNegotiationButtonIndex_ = 1;
        if (currentPlanetContractsLocked()) {
            activeContracts_.clear();
            contractNegotiationUnavailable_ = true;
            changeState(ScreenState::ContractNegotiation);
            return;
        }
        generateCurrentPlanetContracts();
        if (activeContracts_.empty()) {
            changeState(ScreenState::ContractMenu);
            return;
        }
        changeState(ScreenState::ContractNegotiation);
    }

    void generateCurrentPlanetContracts() {
        activeContracts_.clear();
        if (planets_.empty()) {
            return;
        }

        const PlanetRecord& currentPlanet =
            planets_[std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1)];
        if (currentPlanet.contractAvailableFlag == 0) {
            return;
        }
        if (currentPlanetContractsLocked()) {
            return;
        }
        if (houseNegativeCounters_[std::min<size_t>(currentPlanet.houseId, houseNegativeCounters_.size() - 1u)] > 7) {
            return;
        }

        std::mt19937 rng(contractGenerationSeed(currentPlanet));
        size_t offerCount = 2u + static_cast<size_t>(rng() % 3u);
        if (currentYear_ == 3028 && currentMonth_ >= 7) {
            offerCount = 3u + static_cast<size_t>(rng() % 3u);
        } else if (currentYear_ > 3028) {
            offerCount = 3u + static_cast<size_t>(rng() % 3u);
        }

        activeContracts_.reserve(offerCount);
        for (size_t slot = 0; slot < offerCount; ++slot) {
            activeContracts_.push_back(generateContractOffer(currentPlanet, slot, rng));
        }
    }

    uint32_t contractGenerationSeed(const PlanetRecord& planet) const {
        uint32_t seed = 0x6D575243u;
        seed ^= static_cast<uint32_t>(planet.tableOrder) * 0x9E3779B9u;
        seed ^= static_cast<uint32_t>(planet.planetNumber) * 0x85EBCA6Bu;
        seed ^= static_cast<uint32_t>(currentYear_) * 0xC2B2AE35u;
        seed ^= static_cast<uint32_t>(currentMonth_ + 1) * 0x27D4EB2Du;
        seed ^= static_cast<uint32_t>(currentMonthDayCounter_) * 0x165667B1u;
        seed ^= static_cast<uint32_t>(currentPlanetVisitSerial_) * 0xD3A2646Cu;
        return seed;
    }

    ContractOffer generateContractOffer(const PlanetRecord& currentPlanet, size_t slot, std::mt19937& rng) const {
        ContractOffer offer;
        offer.employerHouse = std::min<uint8_t>(currentPlanet.houseId, 4);
        offer.targetHouse = static_cast<uint8_t>(rng() % 5u);
        offer.targetPlanet = contractTargetPlanetName(offer.targetHouse, rng);

        const bool lateCampaign = currentYear_ > 3028 || (currentYear_ == 3028 && currentMonth_ >= 7);
        const bool canUseExtended = lateCampaign && ownedMechs_.size() >= 4;
        const ContractMissionDefinition& mission = chooseContractMission(canUseExtended, rng);
        offer.missionName = mission.name;
        offer.hasHostileTargetHouse = contractMissionHasHostileTargetHouse(mission);

        int score = contractForceScore();
        if (slot == 1u || slot == 4u) {
            score = score + score / 2;
        } else if (slot == 2u || slot == 5u) {
            score = std::max(1, score / 2);
        }
        assignContractEnemyCounts(score, offer, static_cast<uint32_t>(rng()));
        if (mission.extended) {
            offer.heavyCount *= 3;
            offer.mediumCount *= 3;
            offer.lightCount *= 3;
        }

        offer.priceK = contractBasePriceK(score, offer.employerHouse, static_cast<uint32_t>(rng()));
        if (mission.extended) {
            offer.priceK = std::min(kContractMaxPriceK, offer.priceK * 3);
        }
        offer.salvagePercent = contractDefaultSalvagePercent(offer.employerHouse, static_cast<uint32_t>(rng()));
        offer.advancePercent = contractDefaultAdvancePercent(offer.employerHouse, static_cast<uint32_t>(rng()));
        offer.housePriceK = offer.priceK;
        offer.houseSalvagePercent = offer.salvagePercent;
        offer.houseAdvancePercent = offer.advancePercent;
        return offer;
    }

    void handleCrewMenuClick(int screenX, int screenY) {
        const int mechSlot = hitCrewMechSlot(screenX, screenY);
        if (mechSlot >= 0 && crewMembers_[static_cast<size_t>(mechSlot)].hired) {
            if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                if (crewSelectionIndex_ == static_cast<size_t>(mechSlot)) {
                    applyCrewAssignmentOption();
                } else {
                    openCrewAssignment(static_cast<size_t>(mechSlot));
                }
            } else {
                crewSelectionIndex_ = static_cast<size_t>(mechSlot);
                openCrewAssignment(crewSelectionIndex_);
            }
            return;
        }

        if (hitRect(kCrewDoneButtonRect, screenX, screenY)) {
            if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                closeCrewAssignment();
            } else {
                crewSelectionIndex_ = kCrewDoneSelectionIndex;
                activateCrewSelection();
            }
        }
    }

    void activateStatusMenuItem(size_t itemIndex) {
        const MenuAction action = kStatusMenuItems[itemIndex].action;
        if (action == MenuAction::Crew) {
            changeState(ScreenState::CrewMenu);
        } else if (action == MenuAction::NewsNet) {
            openNewsNet();
        } else if (action == MenuAction::Continue) {
            returnToMainMenu();
        }
    }

    void activateMechLabMenuItem(size_t itemIndex) {
        const MenuAction action = kMechLabMenuItems[itemIndex].action;
        if (action == MenuAction::ReviewMechs) {
            selectedMechIndex_ = 0;
            changeState(ScreenState::MechReviewList);
        } else if (action == MenuAction::ExtraAmmo) {
            extraAmmoSelectionIndex_ = 0;
            changeState(ScreenState::MechExtraAmmo);
        } else if (action == MenuAction::BuyMechs) {
            openCurrentPlanetMechMarket();
        } else if (action == MenuAction::Continue) {
            if (tryStartMechBayExitStory()) {
                return;
            }
            returnToMainMenu();
        }
    }

    void openCurrentPlanetMechMarket() {
        ensureCurrentPlanetMechMarket();
        selectedMarketMechIndex_ = 0;
        mechMarketScrollOffset_ = 0;
        changeState(ScreenState::MechBuyList);
    }

    void activateExtraAmmoSelection() {
        if (extraAmmoSelectionIndex_ >= kAmmoDefinitions.size()) {
            mechLabMenuIndex_ = 1;
            changeState(ScreenState::MechLabMenu);
            return;
        }
        buySelectedExtraAmmo();
    }

    void buySelectedExtraAmmo() {
        if (extraAmmoSelectionIndex_ >= kAmmoDefinitions.size()) {
            return;
        }
        if (extraAmmoInHold_[extraAmmoSelectionIndex_] >= kExtraAmmoMaxInHold) {
            return;
        }

        const uint32_t cost = extraAmmoCost(extraAmmoSelectionIndex_);
        if (cost == 0 || playerWealth_ < cost) {
            return;
        }

        playerWealth_ -= cost;
        ++extraAmmoInHold_[extraAmmoSelectionIndex_];
    }

    void activateMechReviewSelection() {
        const size_t doneIndex = ownedMechs_.size();
        if (selectedMechIndex_ == doneIndex || ownedMechs_.empty()) {
            changeState(ScreenState::MechLabMenu);
            return;
        }

        selectedMechIndex_ = std::min(selectedMechIndex_, ownedMechs_.size() - 1u);
        mechStatusMenuIndex_ = 0;
        changeState(ScreenState::MechStatus);
    }

    void activateMechStatusSelection() {
        if (mechStatusMenuIndex_ == 0) {
            repairSelectionIndex_ = 0;
            changeState(ScreenState::MechRepairStatus);
        } else if (mechStatusMenuIndex_ == 1) {
            repairAllSelectedMech();
        } else if (mechStatusMenuIndex_ == 2) {
            reloadSelectionIndex_ = 0;
            changeState(ScreenState::MechReloadPrompt);
        } else if (mechStatusMenuIndex_ == 3) {
            sellOfferSelectionIndex_ = 0;
            changeState(ScreenState::MechSellOffer);
        } else if (mechStatusMenuIndex_ == 4) {
            changeState(ScreenState::MechReviewList);
        }
    }

    void activateMechRepairSelection() {
        changeState(ScreenState::MechStatus);
    }

    void activateMechReloadSelection() {
        if (reloadSelectionIndex_ == 0) {
            reloadSelectedMech();
            return;
        }
        mechStatusMenuIndex_ = 2;
        changeState(ScreenState::MechStatus);
    }

    void reloadSelectedMech() {
        OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            changeState(ScreenState::MechReviewList);
            return;
        }

        const uint32_t cost = reloadCost(*mech);
        if (cost > 0 && playerWealth_ >= cost) {
            playerWealth_ -= cost;
            const std::array<bool, 6> ammoTypes = ammoTypesForMech(*mech);
            for (size_t i = 0; i < ammoTypes.size(); ++i) {
                if (ammoTypes[i]) {
                    mech->ammoPacks[i] = kMechAmmoMaxPacks;
                }
            }
        }

        mechStatusMenuIndex_ = 0;
        reloadSelectionIndex_ = 0;
        changeState(ScreenState::MechStatus);
    }

    void activateMechSellOfferSelection() {
        if (sellOfferSelectionIndex_ != 0) {
            mechStatusMenuIndex_ = 3;
            changeState(ScreenState::MechStatus);
            return;
        }

        sellSelectedMech();
    }

    void sellSelectedMech() {
        if (selectedMechIndex_ >= ownedMechs_.size()) {
            changeState(ScreenState::MechLabMenu);
            return;
        }

        OwnedMech soldMech = ownedMechs_[selectedMechIndex_];
        soldMech.assignedCrewSlot = -1;
        const uint32_t offer = mechSellOffer(soldMech);
        const uint32_t askingPrice = std::max(offer, mechBuyPrice(soldMech));

        playerWealth_ = std::min<uint64_t>(kMaxPlayerWealth, playerWealth_ + offer);
        currentPlanetMechMarket().mechsForSale.push_back({soldMech, askingPrice});

        ownedMechs_.erase(ownedMechs_.begin() + static_cast<std::ptrdiff_t>(selectedMechIndex_));
        selectedMechIndex_ = ownedMechs_.empty() ? 0 : std::min(selectedMechIndex_, ownedMechs_.size() - 1u);
        mechLabMenuIndex_ = 0;
        changeState(ScreenState::MechLabMenu);
    }

    void activateMechBuyListSelection() {
        const PlanetMechMarket& market = currentPlanetMechMarket();
        if (selectedMarketMechIndex_ >= market.mechsForSale.size()) {
            mechLabMenuIndex_ = 2;
            changeState(ScreenState::MechLabMenu);
            return;
        }

        mechBuyMenuIndex_ = 0;
        changeState(ScreenState::MechBuyStatus);
    }

    void activateMechBuyStatusSelection() {
        if (mechBuyMenuIndex_ == 0) {
            buySelectedMarketMech();
        } else if (mechBuyMenuIndex_ == 1) {
            changeState(ScreenState::MechBuyDamageStatus);
        } else if (mechBuyMenuIndex_ == 2) {
            changeState(ScreenState::MechBuyList);
        }
    }

    void buySelectedMarketMech() {
        PlanetMechMarket& market = currentPlanetMechMarket();
        if (selectedMarketMechIndex_ >= market.mechsForSale.size()) {
            changeState(ScreenState::MechBuyList);
            return;
        }

        const MarketMech& offer = market.mechsForSale[selectedMarketMechIndex_];
        if (ownedMechs_.size() >= kMaxOwnedMechs) {
            changeState(ScreenState::MechBuyTooMany);
            return;
        }
        if (playerWealth_ < offer.askingPrice) {
            changeState(ScreenState::MechBuyCannotAfford);
            return;
        }

        OwnedMech boughtMech = offer.mech;
        boughtMech.assignedCrewSlot = -1;
        boughtMech.repairCost = totalRepairCost(boughtMech);
        playerWealth_ -= offer.askingPrice;
        ownedMechs_.push_back(boughtMech);
        market.mechsForSale.erase(market.mechsForSale.begin() + static_cast<std::ptrdiff_t>(selectedMarketMechIndex_));

        if (!market.mechsForSale.empty() && selectedMarketMechIndex_ >= market.mechsForSale.size()) {
            selectedMarketMechIndex_ = market.mechsForSale.size() - 1u;
        } else if (market.mechsForSale.empty()) {
            selectedMarketMechIndex_ = 0;
        }
        clampMechMarketScroll();
        mechBuyMenuIndex_ = 0;
        changeState(ScreenState::MechBuyList);
    }

    void repairSelectedMechTarget() {
        OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            return;
        }

        RepairTarget target;
        if (!selectedRepairTarget(*mech, target)) {
            return;
        }

        const uint32_t cost = repairTargetCost(*mech, target);
        if (cost == 0 || playerWealth_ < cost) {
            return;
        }

        switch (target.kind) {
        case RepairTargetKind::Engine:
        case RepairTargetKind::Gyros:
        case RepairTargetKind::Sensors:
        case RepairTargetKind::LifeSupport:
        case RepairTargetKind::LeftArmActuator:
        case RepairTargetKind::RightArmActuator:
        case RepairTargetKind::LeftLegActuator:
        case RepairTargetKind::RightLegActuator:
            setComponentCondition(*mech, target.kind, DamageState::Functional);
            break;
        case RepairTargetKind::HeatSink:
            mech->heatSinksWorking = mech->heatSinksTotal;
            break;
        case RepairTargetKind::JumpJets:
            mech->jumpJetsWorking = mech->jumpJetsTotal;
            break;
        case RepairTargetKind::Armor:
            repairArmorStep(*mech);
            break;
        case RepairTargetKind::Weapon:
            if (target.index < mech->weapons.size()) {
                mech->weapons[target.index].condition = DamageState::Functional;
            }
            break;
        }

        playerWealth_ -= cost;
        mech->repairCost = totalRepairCost(*mech);
        const std::vector<RepairTarget> targets = repairableTargets(*mech);
        if (!targets.empty()) {
            repairSelectionIndex_ = std::min(repairSelectionIndex_, targets.size() - 1u);
        } else {
            repairSelectionIndex_ = 0;
            mech->condition = DamageState::Functional;
        }
    }

    void repairAllSelectedMech() {
        OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            return;
        }

        const uint32_t cost = totalRepairCost(*mech);
        if (cost == 0 || playerWealth_ < cost) {
            return;
        }

        mech->engine = DamageState::Functional;
        mech->gyros = DamageState::Functional;
        mech->sensors = DamageState::Functional;
        mech->lifeSupport = DamageState::Functional;
        mech->leftArmActuator = DamageState::Functional;
        mech->rightArmActuator = DamageState::Functional;
        mech->leftLegActuator = DamageState::Functional;
        mech->rightLegActuator = DamageState::Functional;
        mech->heatSinksWorking = mech->heatSinksTotal;
        mech->jumpJetsWorking = mech->jumpJetsTotal;
        mech->armorDamage = {};
        updateArmorPercent(*mech);
        for (MechWeaponStatus& weapon : mech->weapons) {
            weapon.condition = DamageState::Functional;
        }

        playerWealth_ -= cost;
        mech->repairCost = 0;
        mech->condition = DamageState::Functional;
        repairSelectionIndex_ = 0;
    }

    void activateBarMenuItem(size_t itemIndex) {
        const MenuAction action = kBarMenuItems[itemIndex].action;
        if (itemIndex == 0) {
            if (!tryStartOrderDrinkStory()) {
                spendStoryCbills(5);
                beginStory({makeStoryPage(kStoryQuietBarDrink)}, StoryBackdrop::Bar, ScreenState::BarMenu);
            }
        } else if (action == MenuAction::RecruitCrew) {
            openRecruitDialog();
        } else if (action == MenuAction::Continue) {
            if (tryStartBarExitStory()) {
                return;
            }
            returnToMainMenu();
        }
    }

    void openRecruitDialog() {
        barMenuIndex_ = 1;
        ensureRecruitPoolForCurrentPlanet();
        const int candidateIndex = findNextRecruitCandidate();
        if (candidateIndex < 0) {
            activeRecruitIndex_ = -1;
            barDialogState_ = BarDialogState::NoCandidates;
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            debugLog(L"Recruit dialog opened. No candidates on " + currentPlanetName() + L".");
#endif
            return;
        }

        activeRecruitIndex_ = candidateIndex;
        recruitChoiceIndex_ = 0;
        barDialogState_ = BarDialogState::RecruitOffer;
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugLog(
            L"Recruit offer: " +
            recruitPilots_[static_cast<size_t>(activeRecruitIndex_)].name +
            L" on " +
            currentPlanetName());
#endif
    }

    void activateRecruitChoice() {
        if (barDialogState_ != BarDialogState::RecruitOffer || activeRecruitIndex_ < 0) {
            closeBarDialog();
            return;
        }

        const size_t index = static_cast<size_t>(activeRecruitIndex_);
        removeRecruitFromCurrentPool(index);

        if (recruitChoiceIndex_ == 0) {
            if (!hireRecruit(index)) {
                barDialogState_ = BarDialogState::CrewFull;
                activeRecruitIndex_ = -1;
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
                debugLog(L"Recruit hire failed. Crew is full.");
#endif
                return;
            }
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            debugLog(L"Recruit hired: " + recruitPilots_[index].name);
#endif
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        } else {
            debugLog(L"Recruit declined: " + recruitPilots_[index].name);
#endif
        }

        closeBarDialog();
    }

    void closeBarDialog() {
        barDialogState_ = BarDialogState::None;
        activeRecruitIndex_ = -1;
        recruitChoiceIndex_ = 0;
        barMenuIndex_ = 1;
    }

    bool isStorySceneActive() const {
        return !storyPages_.empty() && currentStoryPageIndex_ < storyPages_.size();
    }

    bool storyFlag(uint8_t messageId) const {
        return storyMessageFlags_[messageId] != 0;
    }

    void setStoryFlag(uint8_t messageId) {
        storyMessageFlags_[messageId] = 1;
    }

    bool currentPlanetIs(std::string_view name) const {
        if (planets_.empty()) {
            return name == kFallbackStartingPlanetName;
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return planets_[static_cast<size_t>(index)].name == name;
    }

    std::string currentPlanetNameAscii() const {
        if (planets_.empty()) {
            return std::string(kFallbackStartingPlanetName);
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return planets_[static_cast<size_t>(index)].name;
    }

    bool isStartingStoryPlanet() const {
        const std::string_view name = startingStoryPlanetName_.empty()
            ? kFallbackStartingPlanetName
            : std::string_view(startingStoryPlanetName_);
        return currentPlanetIs(name);
    }

    int chooseNewGameStartingPlanetIndex() const {
        std::vector<int> candidates;
        candidates.reserve(planets_.size());
        for (size_t i = 0; i < planets_.size(); ++i) {
            const PlanetRecord& planet = planets_[i];
            if (planet.contractAvailableFlag != 0 && (planet.houseId == 0 || planet.houseId == 4)) {
                candidates.push_back(static_cast<int>(i));
            }
        }
        if (candidates.empty()) {
            for (size_t i = 0; i < planets_.size(); ++i) {
                if (planets_[i].contractAvailableFlag != 0) {
                    candidates.push_back(static_cast<int>(i));
                }
            }
        }
        if (candidates.empty()) {
            return findPlanetIndexByName(kFallbackStartingPlanetName);
        }

        std::mt19937 rng(makeRecruitmentSeed() ^ 0x53544152u);
        std::uniform_int_distribution<size_t> distribution(0, candidates.size() - 1u);
        return candidates[distribution(rng)];
    }

    bool isMainStoryComplete() const {
        return storyFlag(0x6F);
    }

    void spendStoryCbills(uint64_t amount) {
        playerWealth_ = playerWealth_ > amount ? playerWealth_ - amount : 0;
    }

    std::vector<std::wstring> storyLines(const MwMainTextRef& textRef) const {
        std::vector<std::wstring> lines;
        if (mwMainData_.empty() || textRef.fileOffset >= mwMainData_.size()) {
            lines.push_back(widen(textRef.id));
            return lines;
        }

        std::wstring line;
        bool previousWasCr = false;
        const size_t end = std::min(mwMainData_.size(), textRef.fileOffset + textRef.length);
        for (size_t offset = textRef.fileOffset; offset < end; ++offset) {
            const uint8_t value = mwMainData_[offset];
            if (value == '\r' || value == '\n') {
                if (value == '\n' && previousWasCr) {
                    previousWasCr = false;
                    continue;
                }
                trimStoryLine(line);
                lines.push_back(std::move(line));
                line.clear();
                previousWasCr = value == '\r';
                continue;
            }
            previousWasCr = false;

            if (value == 0) {
                break;
            }
            if (value == '\t') {
                line.push_back(L' ');
            } else if (value >= 0x20 && value <= 0x7E) {
                line.push_back(static_cast<wchar_t>(value));
            }
        }
        trimStoryLine(line);
        lines.push_back(std::move(line));

        lines.erase(
            std::remove_if(
                lines.begin(),
                lines.end(),
                [](const std::wstring& candidate) {
                    return candidate == L"0" || candidate == L"/" || candidate == L"-";
                }),
            lines.end());
        return lines;
    }

    static void trimStoryLine(std::wstring& line) {
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
    }

    static void trimStoryLineStart(std::wstring& line) {
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) {
            line.erase(line.begin());
        }
    }

    StoryPage makeStoryPage(const MwMainTextRef& textRef, std::vector<StoryChoice> choices = {}) const {
        StoryPage page;
        page.lines = storyLines(textRef);
        page.choices = std::move(choices);
        return page;
    }

    StoryPage makeFinalBasePromptPage() const {
        return makeStoryPage(
            kStoryFinalBasePrompt,
            {{L"ATTACK", StoryAction::FinalAttack}, {L"DELAY", StoryAction::FinalDelay}});
    }

    void appendToLastStoryLine(std::vector<std::wstring>& lines, std::wstring_view suffix) const {
        if (lines.empty()) {
            lines.emplace_back(suffix);
        } else {
            if (!lines.back().empty() &&
                !suffix.empty() &&
                storyTextNeedsJoinSpace(lines.back().back(), suffix.front())) {
                lines.back().push_back(L' ');
            }
            lines.back() += suffix;
        }
    }

    static bool storyTextNeedsJoinSpace(wchar_t previous, wchar_t next) {
        const bool previousIsWord = (previous >= L'0' && previous <= L'9') ||
            (previous >= L'A' && previous <= L'Z') ||
            (previous >= L'a' && previous <= L'z');
        const bool nextIsWord = (next >= L'0' && next <= L'9') ||
            (next >= L'A' && next <= L'Z') ||
            (next >= L'a' && next <= L'z');
        return previousIsWord && nextIsWord;
    }

    void appendStoryLines(std::vector<std::wstring>& target, std::vector<std::wstring> source) const {
        target.insert(
            target.end(),
            std::make_move_iterator(source.begin()),
            std::make_move_iterator(source.end()));
    }

    template <size_t Count>
    std::wstring chooseStoryPlanet(const std::array<std::string_view, Count>& names, uint32_t salt) const {
        uint32_t seed = salt;
        seed ^= static_cast<uint32_t>(currentYear_) * 1103515245u;
        seed ^= static_cast<uint32_t>(currentMonth_ + 1) * 12345u;
        seed ^= static_cast<uint32_t>(currentMonthDayCounter_ + 1) * 2654435761u;
        seed ^= static_cast<uint32_t>(std::max(0, currentPlanetIndex_)) * 2246822519u;
        const std::string_view name = names[seed % names.size()];
        return widen(name);
    }

    const std::wstring& grigDestinationPlanet() {
        if (grigDestinationPlanet_.empty()) {
            static constexpr std::array<std::string_view, 3> kKuritaDestinations = {{
                "NEW SAMARKAND",
                "TABAYAMA",
                "DELACRUZ",
            }};
            grigDestinationPlanet_ = chooseStoryPlanet(kKuritaDestinations, 0x47524947u);
        }
        return grigDestinationPlanet_;
    }

    const std::wstring& wendallDestinationPlanet() {
        if (wendallDestinationPlanet_.empty()) {
            static constexpr std::array<std::string_view, 3> kMarikDestinations = {{
                "GIBSON",
                "MOSIRO",
                "SADURNI",
            }};
            wendallDestinationPlanet_ = chooseStoryPlanet(kMarikDestinations, 0x57454E44u);
        }
        return wendallDestinationPlanet_;
    }

    const std::wstring& kearneyDestinationPlanet() {
        if (kearneyDestinationPlanet_.empty()) {
            static constexpr std::array<std::string_view, 3> kDavionDestinations = {{
                "OKEFENOKEE",
                "TANCREDI IV",
                "DELACAMBRE",
            }};
            kearneyDestinationPlanet_ = chooseStoryPlanet(kDavionDestinations, 0x4B454152u);
        }
        return kearneyDestinationPlanet_;
    }

    const std::wstring& blackWidowDestinationPlanet() {
        if (blackWidowDestinationPlanet_.empty()) {
            static constexpr std::array<std::string_view, 2> kBlackWidowDestinations = {{
                "PROSERPINA",
                "THESTRIA",
            }};
            blackWidowDestinationPlanet_ = chooseStoryPlanet(kBlackWidowDestinations, 0x5749444Fu);
        }
        return blackWidowDestinationPlanet_;
    }

    const std::wstring& darkWingDestinationPlanet() {
        if (darkWingDestinationPlanet_.empty()) {
            static constexpr std::array<std::string_view, 2> kDarkWingDestinations = {{
                "KIRCHBACH",
                "ALBIERO",
            }};
            darkWingDestinationPlanet_ = chooseStoryPlanet(kDarkWingDestinations, 0x4457494Eu);
        }
        return darkWingDestinationPlanet_;
    }

    bool isCurrentPlanetName(const std::wstring& name) const {
        return currentPlanetName() == name;
    }

    void beginStory(
        std::vector<StoryPage> pages,
        StoryBackdrop backdrop,
        ScreenState returnState,
        StoryAction defaultAction = StoryAction::None) {
        storyPages_ = std::move(pages);
        currentStoryPageIndex_ = 0;
        currentStoryChoiceIndex_ = 0;
        pendingStoryDefaultAction_ = defaultAction;
        pendingStoryReturnState_ = returnState;
        storyBackdrop_ = backdrop;
        changeState(ScreenState::CampaignMessage);
    }

    bool tryStartOrderDrinkStory() {
        if (isStartingStoryPlanet()) {
            if (!storyFlag(0x06)) {
                setStoryFlag(0x06);
                spendStoryCbills(5);
                beginStory({makeStoryPage(kStoryStartingBarClue1)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
            if (!storyFlag(0x0A)) {
                setStoryFlag(0x0A);
                spendStoryCbills(5);
                beginStory({makeStoryPage(kStoryStartingBarClue2)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
        }

        if (currentPlanetIs("LAND'S END") && storyFlag(0x08)) {
            if (!storyFlag(0x0B)) {
                setStoryFlag(0x0B);
                spendStoryCbills(15);
                beginStory({makeStoryPage(kStoryLandsEndSetup)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
            if (!storyFlag(0x0C)) {
                setStoryFlag(0x0C);
                StoryPage page = makeStoryPage(kStoryLandsEndContact);
                appendToLastStoryLine(page.lines, grigDestinationPlanet() + L".\"");
                beginStory({std::move(page)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
        }

        if (currentPlanetIs("DUSTBALL") && (storyFlag(0x1B) || storyFlag(0x1C))) {
            if (!storyFlag(0x24)) {
                setStoryFlag(0x24);
                spendStoryCbills(25);
                beginStory({makeStoryPage(kStoryStoneArrowInquiry)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
            if (!storyFlag(0x25)) {
                setStoryFlag(0x25);
                spendStoryCbills(75);
                StoryPage page = makeStoryPage(kStoryStoneArrowResult);
                appendToLastStoryLine(page.lines, wendallDestinationPlanet() + L".");
                beginStory({std::move(page)}, StoryBackdrop::Bar, ScreenState::BarMenu);
                return true;
            }
        }

        if (storyFlag(0x29) && isCurrentPlanetName(kearneyDestinationPlanet()) && !storyFlag(0x2D)) {
            setStoryFlag(0x2D);
            setStoryFlag(0x2E);
            beginStory(
                {
                    makeStoryPage(kStoryKearneyBarLead),
                    makeStoryPage(kStoryKearneyMeeting),
                },
                StoryBackdrop::Bar,
                ScreenState::BarMenu);
            return true;
        }

        return false;
    }

    bool tryStartBarEntryStory() {
        if (storyFlag(0x36) && isCurrentPlanetName(blackWidowDestinationPlanet()) && !storyFlag(0x38) && !storyFlag(0x39)) {
            setStoryFlag(0x37);
            beginStory(
                {makeStoryPage(
                    kStoryBlackWidowBar,
                    {
                        {L"ACCEPT HER STORY", StoryAction::AcceptBlackWidowStory},
                        {L"CHALLENGE HER STORY", StoryAction::ChallengeBlackWidowStory},
                    })},
                StoryBackdrop::Bar,
                ScreenState::BarMenu);
            return true;
        }
        return false;
    }

    bool tryStartBarExitStory() {
        if (storyFlag(0x2E) && !storyFlag(0x2F)) {
            setStoryFlag(0x2F);
            beginStory(
                {makeStoryPage(
                    kStoryKearneyAddressPrompt,
                    {{L"FOLLOW ADDRESS", StoryAction::FollowAddress}, {L"FORGET IT", StoryAction::ForgetAddress}})},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            return true;
        }
        return false;
    }

    bool tryStartContractStory() {
        if (currentPlanetIs("GALEDON V") && !storyFlag(0x08)) {
            setStoryFlag(0x08);
            beginStory({makeStoryPage(kStoryGaledonLead)}, StoryBackdrop::Contract, ScreenState::ContractMenu);
            return true;
        }
        return false;
    }

    bool tryStartPostTravelStory() {
        if (currentPlanetIs("ANDER'S MOON") && !isMainStoryComplete()) {
            beginStory({makeStoryPage(kStoryAndersMoonArrest)}, StoryBackdrop::Campaign, ScreenState::MainMenu, StoryAction::GameOver);
            return true;
        }

        if (storyFlag(0x0C) && isCurrentPlanetName(grigDestinationPlanet()) && !storyFlag(0x64)) {
            setStoryFlag(0x64);
            beginStory(
                {
                    makeStoryPage(kStoryGrigEscort),
                    makeStoryPage(kStoryGrigOffer, {{L"YES", StoryAction::GrigYes}, {L"NO", StoryAction::GrigNo}}),
                },
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            return true;
        }

        if (storyFlag(0x0E) && currentPlanetIs("DUSTBALL") && !storyFlag(0x17)) {
            setStoryFlag(0x17);
            beginStory(
                {
                    makeStoryPage(kStoryDustballEntry),
                    makeStoryPage(
                        kStoryDustballPrompt,
                        {{L"FIGHT", StoryAction::DustballFirstFight}, {L"RUN", StoryAction::DustballFirstRun}}),
                },
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            return true;
        }

        if (storyFlag(0x40) && currentPlanetIs("ALBIERO") && !storyFlag(0x41)) {
            setStoryFlag(0x41);
            beginStory(
                {
                    makeStoryPage(kStoryAlbieroRaidIntro),
                    makeStoryPage(kStoryAlbieroMapLead),
                    makeStoryPage(kStoryAlbieroLoading),
                    makeStoryPage(
                        kStoryAlbieroFollowPrompt,
                        {{L"FOLLOW HER", StoryAction::FollowTasha}, {L"STAY DOWN", StoryAction::StayDown}}),
                },
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            return true;
        }

        if (tryStartFinalBasePrompt()) {
            return true;
        }

        return false;
    }

    bool tryStartFinalBasePrompt() {
        if (storyFlag(0x45) && isCurrentPlanetName(darkWingDestinationPlanet())) {
            beginStory({makeFinalBasePromptPage()}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            return true;
        }
        return false;
    }

    void returnToMainMenu(bool allowFinalBasePrompt = true) {
        if (allowFinalBasePrompt && tryStartFinalBasePrompt()) {
            return;
        }
        changeState(ScreenState::MainMenu);
    }

    bool tryStartMechBayStory() {
        std::vector<StoryPage> pages;
        if (storyFlag(0x0A) && !storyFlag(0x07)) {
            setStoryFlag(0x07);
            spendStoryCbills(10);
            pages.push_back(makeStoryPage(kStoryOptionalCrestLore));
        }

        if (storyFlag(0x25) && isCurrentPlanetName(wendallDestinationPlanet()) && !storyFlag(0x26)) {
            setStoryFlag(0x26);
            StoryPage intro = makeStoryPage(kStoryScorpionPilotIntro);
            if (!intro.lines.empty()) {
                if (!intro.lines.front().empty() && intro.lines.front().front() == L',') {
                    intro.lines.front().erase(intro.lines.front().begin());
                }
                trimStoryLineStart(intro.lines.front());
            }

            StoryPage lead = makeStoryPage(kStoryScorpionPilotLead);
            while (!lead.lines.empty() && lead.lines.back().empty()) {
                lead.lines.pop_back();
            }

            std::vector<std::wstring> suffixLines = storyLines(kStoryScorpionPilotLeadSuffix);
            if (!suffixLines.empty()) {
                trimStoryLineStart(suffixLines.front());
                std::wstring destinationLine = kearneyDestinationPlanet();
                if (!destinationLine.empty() &&
                    !suffixLines.front().empty() &&
                    storyTextNeedsJoinSpace(destinationLine.back(), suffixLines.front().front())) {
                    destinationLine.push_back(L' ');
                }
                destinationLine += suffixLines.front();
                lead.lines.push_back(std::move(destinationLine));
                suffixLines.erase(suffixLines.begin());
            } else {
                lead.lines.push_back(kearneyDestinationPlanet());
            }
            appendStoryLines(lead.lines, std::move(suffixLines));
            pages.push_back(std::move(intro));
            pages.push_back(std::move(lead));
        }

        if (!pages.empty()) {
            beginStory(std::move(pages), StoryBackdrop::MechLab, ScreenState::MechLabMenu);
            return true;
        }
        return false;
    }

    bool tryStartMechBayExitStory() {
        if (storyFlag(0x26) && !storyFlag(0x27)) {
            setStoryFlag(0x27);
            beginStory(
                {makeStoryPage(kStorySniperPrompt, {{L"FIGHT", StoryAction::SniperFight}, {L"RUN", StoryAction::SniperRun}})},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            return true;
        }
        return false;
    }

    void moveStoryChoice(int delta) {
        if (!isStorySceneActive()) {
            return;
        }
        const StoryPage& page = storyPages_[currentStoryPageIndex_];
        if (page.choices.empty()) {
            return;
        }
        const size_t count = page.choices.size();
        if (delta < 0) {
            currentStoryChoiceIndex_ = (currentStoryChoiceIndex_ + count - 1u) % count;
        } else {
            currentStoryChoiceIndex_ = (currentStoryChoiceIndex_ + 1u) % count;
        }
    }

    void handleStoryClick(int screenX, int screenY) {
        (void)screenX;
        if (!isStorySceneActive()) {
            return;
        }
        StoryPage& page = storyPages_[currentStoryPageIndex_];
        if (!page.choices.empty()) {
            const int choiceIndex = storyChoiceIndexAtY(page, screenY);
            if (choiceIndex >= 0) {
                currentStoryChoiceIndex_ = static_cast<size_t>(choiceIndex);
            }
        }
        advanceStoryScene();
    }

    int storyChoiceIndexAtY(const StoryPage& page, int screenY) const {
        const int firstChoiceLine = firstStoryChoiceLine(page);
        if (firstChoiceLine < 0) {
            return -1;
        }
        const RectI panel = storyPanelRect(page);
        const int lineStep = storyLineStep(page);
        const int textY = panel.y + 10;
        const int index = (screenY - (textY + firstChoiceLine * lineStep)) / lineStep;
        if (index < 0 || static_cast<size_t>(index) >= page.choices.size()) {
            return -1;
        }
        return index;
    }

    int storyLineStep(const StoryPage& page) const {
        return page.lines.size() > 20 ? 7 : 8;
    }

    RectI storyPanelRect(const StoryPage& page) const {
        const int lineStep = storyLineStep(page);
        const int lineCount = std::max<int>(1, static_cast<int>(page.lines.size()));
        const int maxPanelHeight = storyBackdrop_ == StoryBackdrop::Contract ? 184 : 192;
        const int panelHeight = std::clamp(lineCount * lineStep + 20, 38, maxPanelHeight);
        if (storyBackdrop_ == StoryBackdrop::Contract) {
            return {22, std::max(10, kScreenHeight - panelHeight - 4), 276, panelHeight};
        }
        const int panelY = (kScreenHeight - panelHeight) / 2;
        return {22, panelY, 276, panelHeight};
    }

    int firstStoryChoiceLine(const StoryPage& page) const {
        for (size_t i = 0; i < page.lines.size(); ++i) {
            std::wstring trimmed = page.lines[i];
            while (!trimmed.empty() && trimmed.front() == L' ') {
                trimmed.erase(trimmed.begin());
            }
            for (const StoryChoice& choice : page.choices) {
                if (trimmed == choice.label) {
                    return static_cast<int>(i);
                }
            }
        }
        return page.lines.empty() ? 0 : static_cast<int>(page.lines.size());
    }

    void advanceStoryScene() {
        if (!isStorySceneActive()) {
            return;
        }

        const StoryPage& page = storyPages_[currentStoryPageIndex_];
        if (!page.choices.empty()) {
            const size_t index = std::min(currentStoryChoiceIndex_, page.choices.size() - 1u);
            const StoryAction action = page.choices[index].action;
            clearStoryScene();
            executeStoryAction(action);
            return;
        }

        if (currentStoryPageIndex_ + 1u < storyPages_.size()) {
            ++currentStoryPageIndex_;
            currentStoryChoiceIndex_ = 0;
            return;
        }

        const StoryAction defaultAction = pendingStoryDefaultAction_;
        const ScreenState returnState = pendingStoryReturnState_;
        clearStoryScene();
        if (defaultAction == StoryAction::None) {
            changeState(returnState);
        } else {
            executeStoryAction(defaultAction);
        }
    }

    void clearStoryScene() {
        storyPages_.clear();
        currentStoryPageIndex_ = 0;
        currentStoryChoiceIndex_ = 0;
        pendingStoryDefaultAction_ = StoryAction::None;
    }

    void executeStoryAction(StoryAction action) {
        switch (action) {
        case StoryAction::GameOver:
            beginStory(
                {makeStoryPage(
                    kMissionDeathPromptText,
                    {{L"PLAY AGAIN", StoryAction::RestartCampaign}, {L"QUIT", StoryAction::QuitToDos}})},
                storyBackdrop_,
                ScreenState::MainMenu);
            break;
        case StoryAction::RestartCampaign:
            restartCampaign();
            break;
        case StoryAction::QuitToDos:
            DestroyWindow(hwnd_);
            break;
        case StoryAction::GrigYes:
            setStoryFlag(0x0E);
            beginStory({makeStoryPage(kStoryGrigYes)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::GrigNo:
            setStoryFlag(0x0F);
            beginStory({makeStoryPage(kStoryGrigNo)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::DustballFirstFight:
            setStoryFlag(0x18);
            beginStory(
                {makeStoryPage(
                    kStoryDustballFight,
                    {{L"FIGHT", StoryAction::DustballSecondFight}, {L"RUN", StoryAction::DustballFightThenRun}})},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            break;
        case StoryAction::DustballFirstRun:
            setStoryFlag(0x19);
            beginStory(
                {makeStoryPage(
                    kStoryDustballRun,
                    {{L"FIGHT", StoryAction::DustballSecondFight}, {L"RUN", StoryAction::DustballRunThenRun}})},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            break;
        case StoryAction::DustballSecondFight:
            setStoryFlag(0x1A);
            beginStory({makeStoryPage(kStoryDustballDeath)}, StoryBackdrop::Campaign, ScreenState::MainMenu, StoryAction::GameOver);
            break;
        case StoryAction::DustballFightThenRun:
            setStoryFlag(0x1B);
            beginStory({makeStoryPage(kStoryDustballFightRun)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::DustballRunThenRun:
            setStoryFlag(0x1C);
            beginStory({makeStoryPage(kStoryDustballRunRun)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::SniperFight:
            setStoryFlag(0x28);
            beginStory({makeStoryPage(kStorySniperDeath)}, StoryBackdrop::Campaign, ScreenState::MainMenu, StoryAction::GameOver);
            break;
        case StoryAction::SniperRun:
            setStoryFlag(0x29);
            beginStory({makeStoryPage(kStorySniperRun)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::FollowAddress:
            setStoryFlag(0x30);
            beginStory(
                {makeStoryPage(
                    kStoryKearneyOfficePrompt,
                    {
                        {L"HIDE", StoryAction::OfficeHide},
                        {L"FIGHT", StoryAction::OfficeFight},
                        {L"TALK", StoryAction::OfficeTalk},
                    })},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            break;
        case StoryAction::ForgetAddress:
            setStoryFlag(0x31);
            beginStory({makeStoryPage(kStoryKearneyForget)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::OfficeHide:
            setStoryFlag(0x32);
            beginStory(
                {makeStoryPage(kStoryOfficeHide, {{L"FIGHT", StoryAction::OfficeHideFight}, {L"RUN", StoryAction::OfficeHideRun}})},
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            break;
        case StoryAction::OfficeFight:
        case StoryAction::OfficeHideFight:
            setStoryFlag(0x33);
            beginStory({makeStoryPage(kStoryOfficeFight)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::OfficeTalk:
            setStoryFlag(0x34);
            beginStory({makeStoryPage(kStoryOfficeTalk)}, StoryBackdrop::Campaign, ScreenState::MainMenu, StoryAction::GameOver);
            break;
        case StoryAction::OfficeHideRun:
            setStoryFlag(0x35);
            beginStory({makeStoryPage(kStoryOfficeHideRun)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::AcceptBlackWidowStory:
            setStoryFlag(0x38);
            beginStory({makeStoryPage(kStoryBlackWidowAccept)}, StoryBackdrop::Bar, ScreenState::BarMenu);
            break;
        case StoryAction::ChallengeBlackWidowStory:
            setStoryFlag(0x39);
            beginStory(
                {
                    makeStoryPage(kStoryBlackWidowFight),
                    makeStoryPage(kStoryBlackWidowStandoff),
                    makeStoryPage(kStoryTashaIntro),
                    makeStoryPage(kStoryTashaReveal),
                },
                StoryBackdrop::Bar,
                ScreenState::BarMenu);
            break;
        case StoryAction::FollowTasha:
            setStoryFlag(0x42);
            beginStory(
                {
                    makeStoryPage(kStoryAlbieroCargoDoor),
                    makeStoryPage(
                        kStoryAlbieroTrustPrompt,
                        {{L"TRUST TASHA", StoryAction::TrustTasha}, {L"TRUST KEARNEY", StoryAction::TrustKearney}}),
                },
                StoryBackdrop::Campaign,
                ScreenState::MainMenu);
            break;
        case StoryAction::StayDown:
            setStoryFlag(0x43);
            beginStory({makeStoryPage(kStoryAlbieroStayDown)}, StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        case StoryAction::TrustKearney:
            setStoryFlag(0x44);
            beginStory({makeStoryPage(kStoryTrustKearneyDeath)}, StoryBackdrop::Campaign, ScreenState::MainMenu, StoryAction::GameOver);
            break;
        case StoryAction::TrustTasha: {
            setStoryFlag(0x45);
            playerWealth_ = std::min<uint64_t>(kMaxPlayerWealth, playerWealth_ + 5000000ull);
            StoryPage inroad = makeStoryPage(kStoryOperationInroadDisk);
            appendToLastStoryLine(inroad.lines, darkWingDestinationPlanet() + L".");
            std::vector<StoryPage> pages;
            pages.push_back(makeStoryPage(kStoryTrustTashaSuccess));
            pages.push_back(makeStoryPage(kStoryTashaReward));
            pages.push_back(std::move(inroad));
            if (isCurrentPlanetName(darkWingDestinationPlanet())) {
                pages.push_back(makeFinalBasePromptPage());
            }
            beginStory(std::move(pages), StoryBackdrop::Campaign, ScreenState::MainMenu);
            break;
        }
        case StoryAction::FinalAttack:
            finalMissionStubActive_ = true;
            battleStubButtonIndex_ = 0;
            changeState(ScreenState::MissionBattleStub);
            break;
        case StoryAction::FinalDelay:
            changeState(ScreenState::MainMenu);
            break;
        case StoryAction::None:
        default:
            changeState(pendingStoryReturnState_);
            break;
        }
    }

    void ensureRecruitPoolForCurrentPlanet() {
        if (currentPlanetIndex_ < 0 || static_cast<size_t>(currentPlanetIndex_) >= planets_.size()) {
            return;
        }
        if (planetRecruitPools_.size() != planets_.size()) {
            planetRecruitPools_.assign(planets_.size(), {});
        }

        PlanetRecruitPool& pool = planetRecruitPools_[static_cast<size_t>(currentPlanetIndex_)];
        if (pool.visitSerial == currentPlanetVisitSerial_) {
            return;
        }

        pool = generateRecruitPoolForPlanet(currentPlanetIndex_);
        pool.visitSerial = currentPlanetVisitSerial_;
        pool.monthKey = currentMonthKey();
        for (size_t recruitIndex : pool.originalCandidates) {
            if (recruitIndex < recruitLastPlanetIndex_.size()) {
                recruitLastPlanetIndex_[recruitIndex] = currentPlanetIndex_;
                recruitLastMonthKey_[recruitIndex] = currentMonthKey();
            }
        }
    }

    PlanetRecruitPool generateRecruitPoolForPlanet(int planetIndex) const {
        PlanetRecruitPool pool;
        if (planetIndex < 0 || static_cast<size_t>(planetIndex) >= planets_.size()) {
            return pool;
        }

        // Temporary bar-pool heuristic until the original MW_MAIN.EXE recruit
        // selection algorithm is recovered. The pool is local to a planet visit,
        // scales from 0 to 6 candidates by relative planetary population, avoids
        // candidates seen on the immediately previous planet where practical,
        // and keeps same-portrait pilots separated whenever alternatives exist.
        const size_t targetCount = recruitPoolSizeForPlanet(planets_[static_cast<size_t>(planetIndex)]);
        if (targetCount == 0) {
            return pool;
        }

        std::vector<size_t> candidates = buildRecruitCandidatesForPlanet(planetIndex, true, true);
        if (candidates.size() < targetCount) {
            candidates = buildRecruitCandidatesForPlanet(planetIndex, false, true);
        }
        if (candidates.size() < targetCount) {
            candidates = buildRecruitCandidatesForPlanet(planetIndex, false, false);
        }

        pool.originalCandidates = chooseDiverseRecruitCandidates(candidates, targetCount, planetIndex);
        pool.candidates = pool.originalCandidates;
        return pool;
    }

    std::vector<size_t> buildRecruitCandidatesForPlanet(
        int planetIndex,
        bool avoidPreviousPlanetPool,
        bool respectMonthlyLocation) const {
        std::vector<size_t> candidates;
        for (size_t i = 0; i < recruitPilots_.size(); ++i) {
            if (isRecruitHired(i) || !isRecruitAvailableForReputation(recruitPilots_[i])) {
                continue;
            }
            if (avoidPreviousPlanetPool &&
                std::find(previousPlanetRecruitIndexes_.begin(), previousPlanetRecruitIndexes_.end(), i) !=
                    previousPlanetRecruitIndexes_.end()) {
                continue;
            }
            if (respectMonthlyLocation && isRecruitTemporarilyOnAnotherPlanet(i, planetIndex)) {
                continue;
            }
            candidates.push_back(i);
        }
        return candidates;
    }

    std::vector<size_t> chooseDiverseRecruitCandidates(
        const std::vector<size_t>& candidates,
        size_t targetCount,
        int planetIndex) const {
        std::vector<size_t> selected;
        selected.reserve(std::min(targetCount, candidates.size()));
        while (selected.size() < targetCount && selected.size() < candidates.size()) {
            size_t bestCandidate = candidates.front();
            uint32_t bestScore = UINT32_MAX;
            bool found = false;

            for (size_t candidate : candidates) {
                if (std::find(selected.begin(), selected.end(), candidate) != selected.end()) {
                    continue;
                }

                const RecruitPilot& pilot = recruitPilots_[candidate];
                uint32_t score = recruitCandidateSortKey(candidate, planetIndex);
                const size_t samePortraitCount = static_cast<size_t>(std::count_if(
                    selected.begin(),
                    selected.end(),
                    [&](size_t selectedIndex) {
                        return recruitPilots_[selectedIndex].portraitEntry == pilot.portraitEntry;
                    }));
                score += static_cast<uint32_t>(samePortraitCount * 30000u);
                if (!selected.empty() &&
                    recruitPilots_[selected.back()].portraitEntry == pilot.portraitEntry &&
                    hasCandidateWithDifferentPortrait(candidates, selected, pilot.portraitEntry)) {
                    score += 1000000u;
                }

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    bestCandidate = candidate;
                }
            }

            if (!found) {
                break;
            }
            selected.push_back(bestCandidate);
        }
        return selected;
    }

    bool hasCandidateWithDifferentPortrait(
        const std::vector<size_t>& candidates,
        const std::vector<size_t>& selected,
        uint8_t portraitEntry) const {
        return std::any_of(
            candidates.begin(),
            candidates.end(),
            [&](size_t candidate) {
                return std::find(selected.begin(), selected.end(), candidate) == selected.end() &&
                    recruitPilots_[candidate].portraitEntry != portraitEntry;
            });
    }

    uint32_t recruitCandidateSortKey(size_t recruitIndex, int planetIndex) const {
        const PlanetRecord& planet = planets_[static_cast<size_t>(planetIndex)];
        uint32_t value = static_cast<uint32_t>((recruitIndex + 1u) * 1103515245u);
        value ^= recruitmentSeed_;
        value ^= static_cast<uint32_t>(planet.tableOrder) * 2654435761u;
        value ^= static_cast<uint32_t>(currentMonthKey()) * 2246822519u;
        value ^= static_cast<uint32_t>(currentPlanetVisitSerial_) * 3266489917u;
        value ^= value >> 16;
        value *= 2246822519u;
        value ^= value >> 13;
        return value;
    }

    size_t recruitPoolSizeForPlanet(const PlanetRecord& planet) const {
        if (planets_.size() < 2) {
            return 0;
        }

        size_t lowerPopulationCount = 0;
        for (const PlanetRecord& candidate : planets_) {
            if (candidate.population < planet.population) {
                ++lowerPopulationCount;
            }
        }

        const size_t denominator = planets_.size() - 1u;
        return std::min<size_t>(6u, (lowerPopulationCount * 6u + denominator / 2u) / denominator);
    }

    bool isRecruitTemporarilyOnAnotherPlanet(size_t recruitIndex, int planetIndex) const {
        if (recruitIndex >= recruitLastPlanetIndex_.size()) {
            return false;
        }
        const int lastPlanet = recruitLastPlanetIndex_[recruitIndex];
        if (lastPlanet < 0 || lastPlanet == planetIndex) {
            return false;
        }
        return currentMonthKey() - recruitLastMonthKey_[recruitIndex] < 1;
    }

    void removeRecruitFromCurrentPool(size_t recruitIndex) {
        if (currentPlanetIndex_ < 0 || static_cast<size_t>(currentPlanetIndex_) >= planetRecruitPools_.size()) {
            return;
        }
        std::vector<size_t>& candidates = planetRecruitPools_[static_cast<size_t>(currentPlanetIndex_)].candidates;
        candidates.erase(std::remove(candidates.begin(), candidates.end(), recruitIndex), candidates.end());
    }

    int findNextRecruitCandidate() const {
        if (currentPlanetIndex_ < 0 || static_cast<size_t>(currentPlanetIndex_) >= planetRecruitPools_.size()) {
            return -1;
        }

        const PlanetRecruitPool& pool = planetRecruitPools_[static_cast<size_t>(currentPlanetIndex_)];
        for (size_t candidate : pool.candidates) {
            if (!isRecruitHired(candidate)) {
                return static_cast<int>(candidate);
            }
        }
        return -1;
    }

    bool hireRecruit(size_t recruitIndex) {
        if (recruitIndex >= recruitPilots_.size()) {
            return false;
        }

        auto slot = std::find_if(
            crewMembers_.begin() + 1,
            crewMembers_.end(),
            [](const CrewMember& member) { return !member.hired; });
        if (slot == crewMembers_.end()) {
            return false;
        }

        const RecruitPilot& pilot = recruitPilots_[recruitIndex];
        slot->hired = true;
        slot->name = std::wstring_view(pilot.name);
        slot->gunnery = skillLabel(pilot.gunnerySkill);
        slot->piloting = skillLabel(pilot.pilotingSkill);
        slot->wage = pilot.monthlyWage;
        slot->portraitEntry = pilot.portraitEntry;
        slot->recruitIndex = static_cast<int>(recruitIndex);
        return true;
    }

    bool isRecruitHired(size_t recruitIndex) const {
        return std::any_of(
            crewMembers_.begin(),
            crewMembers_.end(),
            [recruitIndex](const CrewMember& member) {
                return member.hired && member.recruitIndex == static_cast<int>(recruitIndex);
            });
    }

    bool isRecruitAvailableForReputation(const RecruitPilot& pilot) const {
        const uint8_t bestSkill = std::max(pilot.gunnerySkill, pilot.pilotingSkill);
        const uint8_t allowedSkill = static_cast<uint8_t>(std::min<int>(3, playerReputationTier_ + 1));
        return bestSkill <= allowedSkill;
    }

    RectI crewMechSlotRect(size_t slot) const {
        if (slot >= kCrewPilotCellXs.size()) {
            return {};
        }
        return {kCrewPilotCellXs[slot], 106, kCrewCellWidth, 15};
    }

    int hitCrewMechSlot(int screenX, int screenY) const {
        for (size_t i = 0; i < crewMembers_.size(); ++i) {
            if (hitRect(crewMechSlotRect(i), screenX, screenY)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool isCrewSelectionSelectable(size_t index) const {
        return index == kCrewDoneSelectionIndex || (index < crewMembers_.size() && crewMembers_[index].hired);
    }

    void moveCrewSelection(int delta) {
        size_t index = crewSelectionIndex_;
        for (size_t attempt = 0; attempt < kCrewDoneSelectionIndex + 1; ++attempt) {
            index = static_cast<size_t>(
                (static_cast<int>(index) + delta + static_cast<int>(kCrewDoneSelectionIndex + 1)) %
                static_cast<int>(kCrewDoneSelectionIndex + 1));
            if (isCrewSelectionSelectable(index)) {
                crewSelectionIndex_ = index;
                return;
            }
        }
        crewSelectionIndex_ = kCrewDoneSelectionIndex;
    }

    int assignedMechIndexForCrewSlot(size_t crewSlot) const {
        for (size_t i = 0; i < ownedMechs_.size(); ++i) {
            if (ownedMechs_[i].assignedCrewSlot == static_cast<int>(crewSlot)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::wstring_view crewMechLabel(size_t crewSlot) const {
        const int mechIndex = assignedMechIndexForCrewSlot(crewSlot);
        if (mechIndex >= 0) {
            return ownedMechs_[static_cast<size_t>(mechIndex)].name;
        }
        return L"NONE";
    }

    int crewMechImageEntry(size_t crewSlot) const {
        const int mechIndex = assignedMechIndexForCrewSlot(crewSlot);
        if (mechIndex >= 0) {
            return ownedMechs_[static_cast<size_t>(mechIndex)].imageEntry;
        }
        return -1;
    }

    std::vector<CrewAssignmentOption> crewAssignmentOptions(size_t crewSlot) const {
        std::vector<CrewAssignmentOption> options;
        if (crewSlot > 0) {
            options.push_back({CrewAssignmentOptionKind::Dismiss, 0});
        }
        for (size_t i = 0; i < ownedMechs_.size(); ++i) {
            const int assignedSlot = ownedMechs_[i].assignedCrewSlot;
            if (assignedSlot < 0 || assignedSlot == static_cast<int>(crewSlot)) {
                options.push_back({CrewAssignmentOptionKind::Mech, i});
            }
        }
        options.push_back({CrewAssignmentOptionKind::None, 0});
        return options;
    }

    std::wstring crewAssignmentOptionLabel(size_t crewSlot, size_t optionIndex) const {
        const std::vector<CrewAssignmentOption> options = crewAssignmentOptions(crewSlot);
        if (optionIndex >= options.size()) {
            return L"NONE";
        }
        const CrewAssignmentOption& option = options[optionIndex];
        if (option.kind == CrewAssignmentOptionKind::Dismiss) {
            return L"DISMISS";
        }
        if (option.kind == CrewAssignmentOptionKind::Mech && option.mechIndex < ownedMechs_.size()) {
            return std::wstring(ownedMechs_[option.mechIndex].name);
        }
        return L"NONE";
    }

    void openCrewAssignment(size_t crewSlot) {
        if (crewSlot >= crewMembers_.size() || !crewMembers_[crewSlot].hired) {
            return;
        }
        crewInteractionMode_ = CrewInteractionMode::AssignMech;
        crewSelectionIndex_ = crewSlot;
        crewAssignmentIndex_ = 0;

        if (crewSlot == 0) {
            const int assignedMech = assignedMechIndexForCrewSlot(crewSlot);
            const std::vector<CrewAssignmentOption> options = crewAssignmentOptions(crewSlot);
            for (size_t i = 0; i < options.size(); ++i) {
                if (options[i].kind == CrewAssignmentOptionKind::Mech &&
                    static_cast<int>(options[i].mechIndex) == assignedMech) {
                    crewAssignmentIndex_ = i;
                    return;
                }
                if (assignedMech < 0 && options[i].kind == CrewAssignmentOptionKind::None) {
                    crewAssignmentIndex_ = i;
                    return;
                }
            }
        }
    }

    void closeCrewAssignment() {
        crewInteractionMode_ = CrewInteractionMode::Navigate;
        crewAssignmentIndex_ = 0;
    }

    void moveCrewAssignment(int delta) {
        if (crewSelectionIndex_ >= crewMembers_.size()) {
            return;
        }
        const std::vector<CrewAssignmentOption> options = crewAssignmentOptions(crewSelectionIndex_);
        if (options.empty()) {
            crewAssignmentIndex_ = 0;
            return;
        }
        crewAssignmentIndex_ = static_cast<size_t>(
            (static_cast<int>(crewAssignmentIndex_) + delta + static_cast<int>(options.size())) %
            static_cast<int>(options.size()));
    }

    void unassignMechFromCrewSlot(size_t crewSlot) {
        for (OwnedMech& mech : ownedMechs_) {
            if (mech.assignedCrewSlot == static_cast<int>(crewSlot)) {
                mech.assignedCrewSlot = -1;
            }
        }
    }

    void assignMechToCrewSlot(size_t mechIndex, size_t crewSlot) {
        if (mechIndex >= ownedMechs_.size() || crewSlot >= crewMembers_.size() || !crewMembers_[crewSlot].hired) {
            return;
        }
        if (ownedMechs_[mechIndex].assignedCrewSlot >= 0 &&
            ownedMechs_[mechIndex].assignedCrewSlot != static_cast<int>(crewSlot)) {
            return;
        }
        unassignMechFromCrewSlot(crewSlot);
        ownedMechs_[mechIndex].assignedCrewSlot = static_cast<int>(crewSlot);
    }

    void dismissCrewSlot(size_t crewSlot) {
        if (crewSlot == 0 || crewSlot >= crewMembers_.size() || !crewMembers_[crewSlot].hired) {
            return;
        }

        for (OwnedMech& mech : ownedMechs_) {
            if (mech.assignedCrewSlot == static_cast<int>(crewSlot)) {
                mech.assignedCrewSlot = -1;
            } else if (mech.assignedCrewSlot > static_cast<int>(crewSlot)) {
                --mech.assignedCrewSlot;
            }
        }

        for (size_t i = crewSlot; i + 1 < crewMembers_.size(); ++i) {
            crewMembers_[i] = crewMembers_[i + 1];
        }
        crewMembers_.back() = {};

        if (crewSelectionIndex_ < kCrewDoneSelectionIndex && !isCrewSelectionSelectable(crewSelectionIndex_)) {
            crewSelectionIndex_ = kCrewDoneSelectionIndex;
        }
    }

    void applyCrewAssignmentOption() {
        if (crewSelectionIndex_ >= crewMembers_.size() || !crewMembers_[crewSelectionIndex_].hired) {
            closeCrewAssignment();
            return;
        }

        const std::vector<CrewAssignmentOption> options = crewAssignmentOptions(crewSelectionIndex_);
        if (crewAssignmentIndex_ >= options.size()) {
            closeCrewAssignment();
            return;
        }

        const CrewAssignmentOption option = options[crewAssignmentIndex_];
        if (option.kind == CrewAssignmentOptionKind::Dismiss) {
            dismissCrewSlot(crewSelectionIndex_);
        } else if (option.kind == CrewAssignmentOptionKind::Mech) {
            assignMechToCrewSlot(option.mechIndex, crewSelectionIndex_);
        } else {
            unassignMechFromCrewSlot(crewSelectionIndex_);
        }
        closeCrewAssignment();
    }

    void activateCrewSelection() {
        if (crewSelectionIndex_ == kCrewDoneSelectionIndex) {
            changeState(ScreenState::StatusMenu);
            return;
        }
        openCrewAssignment(crewSelectionIndex_);
    }

    void activateSystemMenuItem(size_t itemIndex) {
        const MenuAction action = kSystemMenuItems[itemIndex].action;
        if (action == MenuAction::ExitToDos) {
            DestroyWindow(hwnd_);
        } else if (action == MenuAction::Continue) {
            returnToMainMenu();
        } else if (action == MenuAction::SaveGame) {
            openSaveGameNameInput();
        } else if (action == MenuAction::RestoreGame) {
            openRestoreGameList();
        } else if (action == MenuAction::Restart) {
            barMenuIndex_ = 0;
            planetMenuIndex_ = kPlanetBarIconIndex;
            systemMenuIndex_ = kSystemSaveMenuIndex;
            changeState(ScreenState::ActivisionSplash);
        } else if (action == MenuAction::ToggleSound) {
            soundEnabled_ = !soundEnabled_;
        } else if (action == MenuAction::Detail) {
            detailLevel_ = (detailLevel_ + 1) % 3;
        }
    }

    void openSaveGameNameInput() {
        saveGameNameInput_.clear();
        systemMenuIndex_ = kSystemSaveMenuIndex;
        changeState(ScreenState::SaveGameNameInput);
    }

    void confirmSaveGameName() {
        if (saveGameNameInput_.empty()) {
            return;
        }

        const fs::path path = saveGameDirectory() / (widen(saveGameNameInput_) + L".GAM");
        if (saveCurrentGame(path)) {
            systemMenuIndex_ = kSystemSaveMenuIndex;
            changeState(ScreenState::SystemMenu);
        }
    }

    void openRestoreGameList() {
        refreshRestoreGameSlots();
        restoreGameSelectionIndex_ = 0;
        systemMenuIndex_ = kSystemRestoreMenuIndex;
        changeState(ScreenState::RestoreGameList);
    }

    void handleRestoreGameClick(int screenX, int screenY) {
        const int itemIndex = restoreGameItemAt(screenX, screenY);
        if (itemIndex < 0) {
            return;
        }
        const size_t index = static_cast<size_t>(itemIndex);
        if (restoreGameSelectionIndex_ == index) {
            activateRestoreGameSelection();
            return;
        }
        restoreGameSelectionIndex_ = index;
    }

    void activateRestoreGameSelection() {
        if (restoreGameSelectionIndex_ == kRestoreGameCancelIndex) {
            systemMenuIndex_ = kSystemRestoreMenuIndex;
            changeState(ScreenState::SystemMenu);
            return;
        }

        if (restoreGameSelectionIndex_ >= restoreGameSlots_.size()) {
            return;
        }
        const SaveGameSlot& slot = restoreGameSlots_[restoreGameSelectionIndex_];
        if (!slot.occupied) {
            return;
        }
        loadGameFromFile(slot.path);
    }

    fs::path saveGameDirectory() const {
        return resourceRoot_;
    }

    void refreshRestoreGameSlots() {
        restoreGameSlots_.assign(kGamVisibleSlotCount, {});
        struct FoundSave {
            fs::path path;
            fs::file_time_type modified;
        };
        std::vector<FoundSave> found;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(saveGameDirectory(), error)) {
            if (error) {
                break;
            }
            if (!entry.is_regular_file(error) || error) {
                error.clear();
                continue;
            }
            fs::path path = entry.path();
            std::wstring extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towupper(ch));
            });
            if (extension != L".GAM") {
                continue;
            }
            const auto modified = fs::last_write_time(path, error);
            if (error) {
                error.clear();
                continue;
            }
            found.push_back({std::move(path), modified});
        }

        std::sort(found.begin(), found.end(), [](const FoundSave& left, const FoundSave& right) {
            return left.modified > right.modified;
        });

        const size_t count = std::min(found.size(), kGamVisibleSlotCount);
        for (size_t i = 0; i < count; ++i) {
            SaveGameSlot slot;
            slot.path = found[i].path;
            slot.name = slot.path.stem().wstring();
            if (slot.name.size() > kGamMaxNameChars) {
                slot.name.resize(kGamMaxNameChars);
            }
            slot.occupied = true;
            restoreGameSlots_[i] = std::move(slot);
        }
    }

    int restoreGameItemAt(int screenX, int screenY) const {
        constexpr int panelX = 52;
        constexpr int panelW = 216;
        constexpr int slotX = 118;
        constexpr int firstSlotY = 47;
        constexpr int lineStep = 10;
        constexpr int cancelY = 167;
        if (screenX < panelX || screenX >= panelX + panelW) {
            return -1;
        }
        if (screenY >= cancelY && screenY < cancelY + lineStep) {
            return static_cast<int>(kRestoreGameCancelIndex);
        }
        if (screenX < slotX - 8 || screenY < firstSlotY) {
            return -1;
        }
        const int row = (screenY - firstSlotY) / lineStep;
        if (row < 0 || row >= static_cast<int>(kGamVisibleSlotCount)) {
            return -1;
        }
        return row;
    }

    std::vector<uint8_t> baseGamSaveData() const {
        if (gamRawStateValid_ && gamRawState_.size() == kGamSaveSize) {
            return gamRawState_;
        }

        if (mwMainData_.size() >= kMwMainNewGameTemplateFileOffset + kGamSaveSize) {
            return std::vector<uint8_t>(
                mwMainData_.begin() + static_cast<std::ptrdiff_t>(kMwMainNewGameTemplateFileOffset),
                mwMainData_.begin() + static_cast<std::ptrdiff_t>(kMwMainNewGameTemplateFileOffset + kGamSaveSize));
        }

        const std::array<fs::path, 3> candidates = {{
            resourceRoot_ / L"TEST.GAM",
            fs::current_path() / L"Original" / L"TEST.GAM",
            fs::current_path() / L"Sorted Original Files" / L"GAM" / L"TEST.GAM",
        }};
        for (const fs::path& candidate : candidates) {
            std::error_code error;
            if (!fs::exists(candidate, error) || error) {
                continue;
            }
            try {
                std::vector<uint8_t> data = readFile(candidate);
                if (data.size() == kGamSaveSize) {
                    return data;
                }
            } catch (const std::exception&) {
            }
        }

        std::vector<uint8_t> data(kGamSaveSize, 0);
        for (size_t i = 0; i < kMaxOwnedMechs; ++i) {
            writeU16Le(data, kGamOffsetMechChassisList + i * 2u, 0xFFFF);
        }
        return data;
    }

    bool saveCurrentGame(const fs::path& path) {
        std::vector<uint8_t> data = baseGamSaveData();
        if (data.size() != kGamSaveSize) {
            data.assign(kGamSaveSize, 0);
        }
        writeRuntimeToGamSave(data);

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            status_ = L"Save failed: unable to open " + path.wstring();
            return false;
        }
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!output) {
            status_ = L"Save failed: unable to write " + path.wstring();
            return false;
        }
        gamRawState_ = std::move(data);
        gamRawStateValid_ = true;
        return true;
    }

    void loadGameFromFile(const fs::path& path) {
        try {
            std::vector<uint8_t> data = readFile(path);
            if (data.size() != kGamSaveSize) {
                status_ = L"Load failed: invalid save size";
                return;
            }
            applyGamSaveToRuntime(data);
            gamRawState_ = std::move(data);
            gamRawStateValid_ = true;
            systemMenuIndex_ = kSystemSaveMenuIndex;
            returnToMainMenu();
        } catch (const std::exception& error) {
            status_ = L"Load failed: " + widen(error.what());
        }
    }

    void writeRuntimeToGamSave(std::vector<uint8_t>& data) const {
        const int planetIndex = planets_.empty()
            ? 0
            : std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        if (!planets_.empty()) {
            const PlanetRecord& planet = planets_[static_cast<size_t>(planetIndex)];
            data[kGamOffsetPlanetIndex] =
                static_cast<uint8_t>(planet.planetNumber > 0 ? planet.planetNumber - 1u : 0u);
            data[kGamOffsetCurrentPlanetHouseId] = planet.houseId;
            data[kGamOffsetCurrentPlanetTerrainBand] =
                static_cast<uint8_t>(planet.terrainCode > 0 ? (planet.terrainCode - 1u) / 3u : 0u);
            data[kGamOffsetCurrentPlanetContractAvailable] = planet.contractAvailableFlag;
            data[kGamOffsetMapX] = planet.mapX;
            data[kGamOffsetMapY] = planet.mapY;
        }
        data[kGamOffsetReputation] = playerReputationTier_;
        data[kGamOffsetMonthDayCounter] = static_cast<uint8_t>(std::clamp(currentMonthDayCounter_, 0, 255));
        data[kGamOffsetMonth] = static_cast<uint8_t>(std::clamp(currentMonth_, 0, 11));
        writeU16Le(data, kGamOffsetYear, static_cast<uint16_t>(std::clamp(currentYear_, 0, 65535)));
        data[kGamOffsetPeriodic14DayCounter] =
            static_cast<uint8_t>(std::clamp(currentPeriodic14DayCounter_, 0, 255));
        writeU32Le(data, kGamOffsetMoney, static_cast<uint32_t>(std::min<uint64_t>(playerWealth_, 0xFFFFFFFFull)));
        writeInt16Array(data, kGamOffsetFamilyAttitudes, familyAttitudes_);
        writeInt16Array(data, kGamOffsetPositiveHouseCounters, housePositiveCounters_);
        writeInt16Array(data, kGamOffsetNegativeHouseCounters, houseNegativeCounters_);
        writeU16Le(data, kGamOffsetReputationPoints, playerReputationPoints_);
        writeU16Le(data, kGamOffsetReputationTier, playerReputationTier_);
        writeRuntimeCrewRosterToGamSave(data);

        const size_t mechCount = std::min<size_t>(ownedMechs_.size(), kMaxOwnedMechs);
        writeU16Le(data, kGamOffsetMechCount, static_cast<uint16_t>(mechCount));
        for (size_t slot = 0; slot < kMaxOwnedMechs; ++slot) {
            const size_t chassisOffset = kGamOffsetMechChassisList + slot * 2u;
            const size_t ammoOffset = kGamOffsetMechAmmo + slot * 2u;
            const size_t recordOffset = kGamOffsetMechRecords + slot * kGamMechRecordStride;
            if (slot >= mechCount) {
                writeU16Le(data, chassisOffset, 0xFFFF);
                writeU16Le(data, ammoOffset, 0);
                std::fill(data.begin() + static_cast<std::ptrdiff_t>(recordOffset),
                          data.begin() + static_cast<std::ptrdiff_t>(recordOffset + kGamMechRecordStride),
                          uint8_t{0});
                continue;
            }

            const OwnedMech& mech = ownedMechs_[slot];
            writeU16Le(data, chassisOffset, gamChassisId(mech.chassis));
            writeGamMechRecord(data, recordOffset, mech);
            writeU16Le(data, ammoOffset, static_cast<uint16_t>(std::clamp(gamAmmoCount(mech), 0, 65535)));
        }

        for (size_t i = 0; i < extraAmmoInHold_.size(); ++i) {
            writeU16Le(
                data,
                kGamOffsetExtraAmmo + i * 2u,
                static_cast<uint16_t>(std::clamp(extraAmmoInHold_[i], 0, 65535)));
        }

        data[kGamOffsetSoundDisabled] = soundEnabled_ ? 0 : 1;
        for (size_t i = 0;
             i < storyMessageFlags_.size() && i < kGamKnownMessageFlagCount && kGamOffsetMessageFlags + i < data.size();
             ++i) {
            data[kGamOffsetMessageFlags + i] = storyMessageFlags_[i];
        }
        data[kGamOffsetDetailLevel] = static_cast<uint8_t>(std::clamp(detailLevel_, 0, 2));
    }

    void applyGamSaveToRuntime(const std::vector<uint8_t>& data) {
        const uint8_t planetSaveId = data[kGamOffsetPlanetIndex];
        currentPlanetIndex_ = findPlanetIndexBySaveId(planetSaveId);
        selectedPlanetIndex_ = currentPlanetIndex_;
        pendingTravelPlanetIndex_ = currentPlanetIndex_;
        pendingTravelCost_ = 0;
        pendingTravelDays_ = 0;

        currentMonthDayCounter_ = data[kGamOffsetMonthDayCounter];
        currentMonth_ = std::clamp<int>(data[kGamOffsetMonth], 0, 11);
        currentYear_ = readGamU16Le(data, kGamOffsetYear);
        currentPeriodic14DayCounter_ = data[kGamOffsetPeriodic14DayCounter];
        currentDay_ = std::clamp(currentMonthDayCounter_ / 2 + 1, 1, 31);
        playerWealth_ = readGamU32Le(data, kGamOffsetMoney);
        readInt16Array(data, kGamOffsetFamilyAttitudes, familyAttitudes_);
        readInt16Array(data, kGamOffsetPositiveHouseCounters, housePositiveCounters_);
        readInt16Array(data, kGamOffsetNegativeHouseCounters, houseNegativeCounters_);
        playerReputationPoints_ = readGamU16Le(data, kGamOffsetReputationPoints);
        playerReputationTier_ =
            static_cast<uint8_t>(std::clamp<int>(readGamU16Le(data, kGamOffsetReputationTier), 0, 3));
        if (playerReputationPoints_ == 0 && data[kGamOffsetReputation] <= 3) {
            playerReputationTier_ = std::max<uint8_t>(playerReputationTier_, data[kGamOffsetReputation]);
            playerReputationPoints_ = minimumReputationPointsForTier(playerReputationTier_);
        }

        storyMessageFlags_.fill(0);
        for (size_t i = 0;
             i < storyMessageFlags_.size() && i < kGamKnownMessageFlagCount && kGamOffsetMessageFlags + i < data.size();
             ++i) {
            storyMessageFlags_[i] = data[kGamOffsetMessageFlags + i];
        }
        if (!storyFlag(0x0A)) {
            startingStoryPlanetName_ = currentPlanetNameAscii();
        } else if (startingStoryPlanetName_.empty()) {
            startingStoryPlanetName_ = std::string(kFallbackStartingPlanetName);
        }
        applyOriginalStoryCompatibilityFlags();
        soundEnabled_ = data[kGamOffsetSoundDisabled] == 0;
        detailLevel_ = std::clamp<int>(data[kGamOffsetDetailLevel], 0, 2);

        for (size_t i = 0; i < extraAmmoInHold_.size(); ++i) {
            extraAmmoInHold_[i] =
                static_cast<int>(readGamU16Le(data, kGamOffsetExtraAmmo + i * 2u));
        }

        ownedMechs_.clear();
        const size_t savedMechCount =
            std::min<size_t>(readGamU16Le(data, kGamOffsetMechCount), kMaxOwnedMechs);
        for (size_t slot = 0; slot < savedMechCount; ++slot) {
            const uint16_t saveChassis = readGamU16Le(data, kGamOffsetMechChassisList + slot * 2u);
            ChassisId chassis = ChassisId::Jenner;
            if (!runtimeChassisId(saveChassis, chassis)) {
                continue;
            }
            OwnedMech mech = makeMech(chassis, ownedMechs_.empty() ? 0 : -1);
            readGamMechRecord(data, kGamOffsetMechRecords + slot * kGamMechRecordStride, mech);
            const int ammoCount = static_cast<int>(readGamU16Le(data, kGamOffsetMechAmmo + slot * 2u));
            const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
            for (size_t i = 0; i < ammoTypes.size(); ++i) {
                mech.ammoPacks[i] = ammoTypes[i] ? std::clamp(ammoCount, 0, kMechAmmoMaxPacks) : 0;
            }
            finalizeLoadedMech(mech);
            ownedMechs_.push_back(std::move(mech));
        }
        if (ownedMechs_.empty()) {
            ownedMechs_.push_back(makeStartingJenner(0));
        }

        applyGamCrewRosterToRuntime(data);
        initializeRecruitmentState();
        if (planetMechMarkets_.size() != planets_.size()) {
            planetMechMarkets_.assign(planets_.size(), {});
        }
        activeContracts_.clear();
        acceptedContract_ = {};
        contractAccepted_ = false;
        missionLaunchPending_ = false;
        finalMissionStubActive_ = false;
        storyPages_.clear();
        currentStoryPageIndex_ = 0;
        currentStoryChoiceIndex_ = 0;
        barDialogState_ = BarDialogState::None;
        starmapNameInputActive_ = false;
        starmapNameInput_.clear();
        closeStarmapMenu();
        planetMenuIndex_ = kPlanetBarIconIndex;
        statusMenuIndex_ = 0;
        newsNetButtonIndex_ = 0;
        newsNetMessageIndex_ = 0;
        newsNetShowingNoOther_ = false;
        newsNetDayCharged_ = false;
    }

    void applyOriginalStoryCompatibilityFlags() {
        if (storyFlag(0x43) && !storyFlag(0x41) && currentPlanetIs("ALBIERO")) {
            setStoryFlag(0x45);
            darkWingDestinationPlanet_ = L"ALBIERO";
        }
    }

    void writeRuntimeCrewRosterToGamSave(std::vector<uint8_t>& data) const {
        const uint16_t crewCount = static_cast<uint16_t>(std::clamp<int>(hiredCrewCount(), 1, 4));
        writeU16Le(data, kGamOffsetCrewCount, crewCount);

        for (size_t slot = 0; slot < crewMembers_.size(); ++slot) {
            const CrewMember& member = crewMembers_[slot];
            const bool active = slot < crewCount && member.hired;
            uint16_t pilotId = 0;
            uint16_t assignedMech = 0xFFFF;
            uint8_t gunnery = 0;
            uint8_t piloting = 0;

            if (active) {
                gunnery = skillRank(member.gunnery);
                piloting = skillRank(member.piloting);
                if (slot > 0 && member.recruitIndex >= 0) {
                    pilotId = static_cast<uint16_t>(member.recruitIndex + 1);
                }

                const int mechIndex = assignedMechIndexForCrewSlot(slot);
                if (mechIndex >= 0 && static_cast<size_t>(mechIndex) < ownedMechs_.size()) {
                    assignedMech = static_cast<uint16_t>(mechIndex);
                }
            }

            writeU16Le(data, kGamOffsetCrewPilotIds + slot * 2u, pilotId);
            writeU16Le(data, kGamOffsetCrewGunnerySkills + slot * 2u, gunnery);
            writeU16Le(data, kGamOffsetCrewPilotingSkills + slot * 2u, piloting);
            writeU16Le(data, kGamOffsetCrewAssignedMechs + slot * 2u, assignedMech);
        }
    }

    void applyGamCrewRosterToRuntime(const std::vector<uint8_t>& data) {
        resetCrewRosterForLoadedMechs();

        const size_t crewCount = std::clamp<size_t>(
            static_cast<size_t>(readGamU16Le(data, kGamOffsetCrewCount)),
            1u,
            crewMembers_.size());

        for (size_t slot = 1; slot < crewCount; ++slot) {
            const uint16_t pilotId = readGamU16Le(data, kGamOffsetCrewPilotIds + slot * 2u);
            if (pilotId == 0) {
                continue;
            }

            const size_t recruitIndex = static_cast<size_t>(pilotId - 1u);
            if (recruitIndex >= recruitPilots_.size()) {
                continue;
            }

            const RecruitPilot& pilot = recruitPilots_[recruitIndex];
            const uint8_t gunnery = static_cast<uint8_t>(std::clamp<int>(
                readGamU16Le(data, kGamOffsetCrewGunnerySkills + slot * 2u),
                0,
                3));
            const uint8_t piloting = static_cast<uint8_t>(std::clamp<int>(
                readGamU16Le(data, kGamOffsetCrewPilotingSkills + slot * 2u),
                0,
                3));

            CrewMember& member = crewMembers_[slot];
            member.hired = true;
            member.name = std::wstring_view(pilot.name);
            member.gunnery = skillLabel(gunnery);
            member.piloting = skillLabel(piloting);
            member.wage = monthlyWageForGunnery(gunnery);
            member.portraitEntry = pilot.portraitEntry;
            member.recruitIndex = static_cast<int>(recruitIndex);
            member.missionExperience = 0;
        }

        for (OwnedMech& mech : ownedMechs_) {
            mech.assignedCrewSlot = -1;
        }

        std::array<bool, kMaxOwnedMechs> mechAssigned = {};
        for (size_t slot = 0; slot < crewMembers_.size(); ++slot) {
            if (!crewMembers_[slot].hired) {
                continue;
            }

            const uint16_t assignedMech = readGamU16Le(data, kGamOffsetCrewAssignedMechs + slot * 2u);
            if (assignedMech == 0xFFFF ||
                static_cast<size_t>(assignedMech) >= ownedMechs_.size() ||
                mechAssigned[assignedMech]) {
                continue;
            }

            ownedMechs_[assignedMech].assignedCrewSlot = static_cast<int>(slot);
            mechAssigned[assignedMech] = true;
        }

        if (!ownedMechs_.empty() &&
            crewMembers_[0].hired &&
            assignedMechIndexForCrewSlot(0) < 0 &&
            !mechAssigned[0]) {
            ownedMechs_[0].assignedCrewSlot = 0;
        }
    }

    void resetCrewRosterForLoadedMechs() {
        crewMembers_ = {{
            {true, L"G BRAVER", L"POOR", L"POOR", 0, kCrewPlayerPortraitEntry, -1},
            {},
            {},
            {},
        }};
        bool assigned = false;
        for (OwnedMech& mech : ownedMechs_) {
            mech.assignedCrewSlot = assigned ? -1 : 0;
            assigned = true;
        }
        crewInteractionMode_ = CrewInteractionMode::Navigate;
        crewSelectionIndex_ = kCrewDoneSelectionIndex;
        crewAssignmentIndex_ = 0;
    }

    int findPlanetIndexBySaveId(uint8_t saveId) const {
        for (size_t i = 0; i < planets_.size(); ++i) {
            if (planets_[i].planetNumber == static_cast<uint8_t>(saveId + 1u)) {
                return static_cast<int>(i);
            }
        }
        return findPlanetIndexByName(kFallbackStartingPlanetName);
    }

    static uint16_t gamChassisId(ChassisId chassis) {
        switch (chassis) {
        case ChassisId::Locust:
            return 0;
        case ChassisId::Wasp:
            return 1;
        case ChassisId::Jenner:
            return 2;
        case ChassisId::PhoenixHawk:
            return 3;
        case ChassisId::ShadowHawk:
            return 4;
        case ChassisId::Wolverine:
            return 5;
        case ChassisId::Rifleman:
            return 6;
        case ChassisId::Warhammer:
            return 7;
        case ChassisId::Marauder:
            return 8;
        case ChassisId::Battlemaster:
            return 9;
        }
        return 2;
    }

    static bool runtimeChassisId(uint16_t saveId, ChassisId& chassis) {
        switch (saveId) {
        case 0:
            chassis = ChassisId::Locust;
            return true;
        case 1:
            chassis = ChassisId::Wasp;
            return true;
        case 2:
            chassis = ChassisId::Jenner;
            return true;
        case 3:
            chassis = ChassisId::PhoenixHawk;
            return true;
        case 4:
            chassis = ChassisId::ShadowHawk;
            return true;
        case 5:
            chassis = ChassisId::Wolverine;
            return true;
        case 6:
            chassis = ChassisId::Rifleman;
            return true;
        case 7:
            chassis = ChassisId::Warhammer;
            return true;
        case 8:
            chassis = ChassisId::Marauder;
            return true;
        case 9:
            chassis = ChassisId::Battlemaster;
            return true;
        default:
            return false;
        }
    }

    static uint16_t readGamU16Le(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + 1u >= data.size()) {
            return 0;
        }
        return static_cast<uint16_t>(data[offset] | (data[offset + 1u] << 8));
    }

    static uint32_t readGamU32Le(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + 3u >= data.size()) {
            return 0;
        }
        return static_cast<uint32_t>(data[offset]) |
            (static_cast<uint32_t>(data[offset + 1u]) << 8) |
            (static_cast<uint32_t>(data[offset + 2u]) << 16) |
            (static_cast<uint32_t>(data[offset + 3u]) << 24);
    }

    static void writeU16Le(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
        if (offset + 1u >= data.size()) {
            return;
        }
        data[offset] = static_cast<uint8_t>(value & 0xFFu);
        data[offset + 1u] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    }

    static void writeU32Le(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
        if (offset + 3u >= data.size()) {
            return;
        }
        data[offset] = static_cast<uint8_t>(value & 0xFFu);
        data[offset + 1u] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        data[offset + 2u] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        data[offset + 3u] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    }

    static void writeInt16Array(
        std::vector<uint8_t>& data,
        size_t offset,
        const std::array<int, 5>& values) {
        for (size_t i = 0; i < values.size(); ++i) {
            const int value = std::clamp(values[i], -32768, 32767);
            writeU16Le(data, offset + i * 2u, static_cast<uint16_t>(static_cast<int16_t>(value)));
        }
    }

    static void readInt16Array(
        const std::vector<uint8_t>& data,
        size_t offset,
        std::array<int, 5>& values) {
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<int16_t>(readGamU16Le(data, offset + i * 2u));
        }
    }

    static uint8_t gamDamageState(DamageState state) {
        switch (state) {
        case DamageState::Functional:
            return 0;
        case DamageState::LightDamage:
            return 1;
        case DamageState::HeavyDamage:
            return 2;
        case DamageState::Junk:
            return 3;
        }
        return 0;
    }

    static DamageState runtimeDamageState(uint8_t value) {
        switch (std::min<uint8_t>(value, 3)) {
        case 1:
            return DamageState::LightDamage;
        case 2:
            return DamageState::HeavyDamage;
        case 3:
            return DamageState::Junk;
        default:
            return DamageState::Functional;
        }
    }

    static int gamAmmoCount(const OwnedMech& mech) {
        const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
        for (size_t i = 0; i < ammoTypes.size(); ++i) {
            if (ammoTypes[i]) {
                return mech.ammoPacks[i];
            }
        }
        return 0;
    }

    static void writeGamMechRecord(std::vector<uint8_t>& data, size_t offset, const OwnedMech& mech) {
        if (offset + kGamMechRecordStride > data.size()) {
            return;
        }
        data[offset + 0x00u] = gamDamageState(mech.engine);
        data[offset + 0x01u] = gamDamageState(mech.gyros);
        data[offset + 0x02u] = gamDamageState(mech.sensors);
        data[offset + 0x03u] = gamDamageState(mech.lifeSupport);
        data[offset + 0x04u] =
            static_cast<uint8_t>(std::clamp(mech.heatSinksTotal - mech.heatSinksWorking, 0, 255));
        data[offset + 0x05u] = gamDamageState(mech.leftArmActuator);
        data[offset + 0x06u] = gamDamageState(mech.rightArmActuator);
        data[offset + 0x07u] = gamDamageState(mech.leftLegActuator);
        data[offset + 0x08u] = gamDamageState(mech.rightLegActuator);
        data[offset + 0x09u] =
            static_cast<uint8_t>(std::clamp(mech.jumpJetsTotal - mech.jumpJetsWorking, 0, 255));
        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            data[offset + 0x0Au + i] = gamDamageState(mech.weapons[i].condition);
        }
        for (size_t i = 0; i < mech.armorDamage.size(); ++i) {
            data[offset + 0x14u + i] =
                static_cast<uint8_t>(std::clamp(mech.armorDamage[i], 0, kArmorDamageMaxLevel));
        }
    }

    static void readGamMechRecord(const std::vector<uint8_t>& data, size_t offset, OwnedMech& mech) {
        if (offset + kGamMechRecordStride > data.size()) {
            return;
        }
        mech.engine = runtimeDamageState(data[offset + 0x00u]);
        mech.gyros = runtimeDamageState(data[offset + 0x01u]);
        mech.sensors = runtimeDamageState(data[offset + 0x02u]);
        mech.lifeSupport = runtimeDamageState(data[offset + 0x03u]);
        mech.heatSinksWorking = std::clamp(
            mech.heatSinksTotal - static_cast<int>(data[offset + 0x04u]),
            0,
            mech.heatSinksTotal);
        mech.leftArmActuator = runtimeDamageState(data[offset + 0x05u]);
        mech.rightArmActuator = runtimeDamageState(data[offset + 0x06u]);
        mech.leftLegActuator = runtimeDamageState(data[offset + 0x07u]);
        mech.rightLegActuator = runtimeDamageState(data[offset + 0x08u]);
        mech.jumpJetsWorking = std::clamp(
            mech.jumpJetsTotal - static_cast<int>(data[offset + 0x09u]),
            0,
            mech.jumpJetsTotal);
        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            mech.weapons[i].condition = mech.weapons[i].weapon.empty()
                ? DamageState::Functional
                : runtimeDamageState(data[offset + 0x0Au + i]);
        }
        for (size_t i = 0; i < mech.armorDamage.size(); ++i) {
            mech.armorDamage[i] = std::clamp<int>(data[offset + 0x14u + i], 0, kArmorDamageMaxLevel);
        }
    }

    void finalizeLoadedMech(OwnedMech& mech) const {
        updateArmorPercent(mech);
        mech.repairCost = totalRepairCost(mech);
        mech.condition =
            mech.engine == DamageState::Junk ||
                    mech.gyros == DamageState::Junk ||
                    mech.sensors == DamageState::Junk ||
                    mech.lifeSupport == DamageState::Junk ||
                    mech.leftLegActuator == DamageState::Junk ||
                    mech.rightLegActuator == DamageState::Junk
                ? DamageState::Junk
                : DamageState::Functional;
    }

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
    void cheatMoney() {
        playerWealth_ = 10000000;
        debugLog(L"Cheat money applied. Wealth: " + formatWealth(playerWealth_));
    }

    void cheatPilotSkillPoor() {
        setHiredPilotSkills(0);
    }

    void cheatPilotSkillAverage() {
        setHiredPilotSkills(1);
    }

    void cheatPilotSkillGood() {
        setHiredPilotSkills(2);
    }

    void cheatPilotSkillExcellent() {
        setHiredPilotSkills(3);
    }

    void setHiredPilotSkills(uint8_t skill) {
        size_t changed = 0;
        for (size_t i = 0; i < crewMembers_.size(); ++i) {
            CrewMember& member = crewMembers_[i];
            if (!member.hired) {
                continue;
            }

            member.gunnery = skillLabel(skill);
            member.piloting = skillLabel(skill);
            if (member.recruitIndex >= 0 && static_cast<size_t>(member.recruitIndex) < recruitPilots_.size()) {
                RecruitPilot& pilot = recruitPilots_[static_cast<size_t>(member.recruitIndex)];
                pilot.gunnerySkill = skill;
                pilot.pilotingSkill = skill;
                pilot.monthlyWage = monthlyWageForGunnery(skill);
                member.wage = pilot.monthlyWage;
            } else if (i > 0) {
                member.wage = monthlyWageForGunnery(skill);
            }
            ++changed;
        }

        debugLog(
            L"Cheat exp applied. Hired pilots changed: " +
            std::to_wstring(changed) +
            L", skill: " +
            std::wstring(skillLabel(skill)));
    }

    void cheatAddMech(ChassisId chassis, std::string_view command) {
        if (ownedMechs_.size() >= kMaxOwnedMechs) {
            debugLog(L"Cheat " + widen(command) + L" ignored. Owned mech limit reached.");
            return;
        }

        ownedMechs_.push_back(makeMech(chassis, -1));
        randomizeStartingMechDamage(ownedMechs_.back());
        debugLog(L"Cheat " + widen(command) + L" applied. Owned mechs: " + std::to_wstring(ownedMechs_.size()));
    }

    void cheatAddAllMechs() {
        size_t added = 0;
        for (const MechDefinition& definition : kMechDefinitions) {
            if (ownedMechs_.size() >= kMaxOwnedMechs) {
                break;
            }

            ownedMechs_.push_back(makeMech(definition.chassis, -1));
            randomizeStartingMechDamage(ownedMechs_.back());
            ++added;
        }

        debugLog(
            L"Cheat mech_all applied. Added: " +
            std::to_wstring(added) +
            L", owned mechs: " +
            std::to_wstring(ownedMechs_.size()));
    }

    void cheatAddLocust() {
        cheatAddMech(ChassisId::Locust, "mech_locust");
    }

    void cheatAddJenner() {
        cheatAddMech(ChassisId::Jenner, "mech_jenner");
    }

    void cheatAddPhoenixHawk() {
        cheatAddMech(ChassisId::PhoenixHawk, "mech_phoenixhawk");
    }

    void cheatAddShadowHawk() {
        cheatAddMech(ChassisId::ShadowHawk, "mech_shadowhawk");
    }

    void cheatAddRifleman() {
        cheatAddMech(ChassisId::Rifleman, "mech_rifleman");
    }

    void cheatAddWarhammer() {
        cheatAddMech(ChassisId::Warhammer, "mech_warhammer");
    }

    void cheatAddMarauder() {
        cheatAddMech(ChassisId::Marauder, "mech_marauder");
    }

    void cheatAddBattlemaster() {
        cheatAddMech(ChassisId::Battlemaster, "mech_battlemaster");
    }

    void cheatReputationRisky() {
        setPlayerReputation(0);
    }

    void cheatReputationWorthWatching() {
        setPlayerReputation(1);
    }

    void cheatReputationVeteran() {
        setPlayerReputation(2);
    }

    void cheatReputationElite() {
        setPlayerReputation(3);
    }

    void setPlayerReputation(uint8_t reputation) {
        playerReputationTier_ = std::min<uint8_t>(reputation, 3);
        playerReputationPoints_ = minimumReputationPointsForTier(playerReputationTier_);
        debugLog(L"Cheat reputation applied: " + std::wstring(reputationLabel()));
    }

    void cheatNextMonth() {
        payMonthlyCrewWages();
        ++currentMonth_;
        if (currentMonth_ >= kCampaignMonthsPerYear) {
            currentMonth_ = 0;
            ++currentYear_;
        }
        currentMonthDayCounter_ = kStartingMonthDayCounter;
        currentPeriodic14DayCounter_ = kStartingPeriodic14DayCounter;
        currentDay_ = 1;
        if (state_ == ScreenState::NewsNet) {
            rebuildNewsNetMessages();
        }
        debugLog(L"Cheat month applied. Date: " + campaignDateLabel());
    }
#endif

    void openNewsNet() {
        rebuildNewsNetMessages();
        newsNetButtonIndex_ = 0;
        newsNetShowingNoOther_ = false;
        newsNetNoOtherDirection_ = 0;
        newsNetMessageIndex_ = 0;
        newsNetDayCharged_ = false;
        changeState(ScreenState::NewsNet);
    }

    void closeNewsNet() {
        if (!newsNetDayCharged_) {
            advanceCampaignDays(1);
            newsNetDayCharged_ = true;
            rebuildNewsNetMessages();
        }
        changeState(ScreenState::StatusMenu);
    }

    void activateNewsNetButton(size_t buttonIndex) {
        if (buttonIndex == 0) {
            if (newsNetShowingNoOther_ && newsNetNoOtherDirection_ > 0) {
                newsNetShowingNoOther_ = false;
                newsNetNoOtherDirection_ = 0;
            } else if (newsNetMessageIndex_ <= 0) {
                newsNetShowingNoOther_ = true;
                newsNetNoOtherDirection_ = -1;
            } else {
                --newsNetMessageIndex_;
                newsNetShowingNoOther_ = false;
                newsNetNoOtherDirection_ = 0;
            }
        } else if (buttonIndex == 1) {
            if (newsNetShowingNoOther_ && newsNetNoOtherDirection_ < 0) {
                newsNetShowingNoOther_ = false;
                newsNetNoOtherDirection_ = 0;
            } else if (newsNetMessageIndex_ + 1 >= static_cast<int>(activeNewsNetMessageIndexes_.size())) {
                newsNetShowingNoOther_ = true;
                newsNetNoOtherDirection_ = 1;
            } else {
                ++newsNetMessageIndex_;
                newsNetShowingNoOther_ = false;
                newsNetNoOtherDirection_ = 0;
            }
        } else {
            closeNewsNet();
        }
    }

    int hitStarmapPlanet(int screenX, int screenY) const {
        for (size_t i = 0; i < planets_.size(); ++i) {
            const PlanetRecord& planet = planets_[i];
            if (std::abs(screenX - static_cast<int>(planet.mapX)) <= 2 &&
                std::abs(screenY - static_cast<int>(planet.mapY)) <= 2) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void beginTravelToSelectedPlanet() {
        if (planets_.empty()) {
            return;
        }
        pendingTravelPlanetIndex_ = std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        if (pendingTravelPlanetIndex_ == currentPlanetIndex_) {
            return;
        }
        pendingTravelCost_ = travelCostBetween(currentPlanetIndex_, pendingTravelPlanetIndex_);
        pendingTravelDays_ = travelDaysBetween(currentPlanetIndex_, pendingTravelPlanetIndex_);
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugLog(
            L"Travel planned: " +
            currentPlanetName() +
            L" -> " +
            widen(planets_[static_cast<size_t>(pendingTravelPlanetIndex_)].name) +
            L", days=" +
            std::to_wstring(pendingTravelDays_) +
            L", cost=" +
            formatWealth(pendingTravelCost_));
#endif
        changeState(ScreenState::TravelRoutePreview);
    }

    int findPlanetIndexByName(std::string_view name) const {
        const auto it = std::find_if(
            planets_.begin(),
            planets_.end(),
            [name](const PlanetRecord& planet) { return planet.name == name; });
        if (it == planets_.end()) {
            return 0;
        }
        return static_cast<int>(std::distance(planets_.begin(), it));
    }

    int findPlanetIndexByNameInsensitive(std::string_view name) const {
        const std::string normalizedName = normalizePlanetNameInput(name);
        if (normalizedName.empty()) {
            return -1;
        }

        for (size_t i = 0; i < planets_.size(); ++i) {
            if (normalizePlanetNameInput(planets_[i].name) == normalizedName) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static std::string normalizePlanetNameInput(std::string_view text) {
        size_t begin = 0;
        while (begin < text.size() && text[begin] <= ' ') {
            ++begin;
        }
        size_t end = text.size();
        while (end > begin && text[end - 1] <= ' ') {
            --end;
        }

        std::string result;
        result.reserve(end - begin);
        bool lastWasSpace = false;
        for (size_t i = begin; i < end; ++i) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (std::isspace(ch)) {
                if (!lastWasSpace) {
                    result.push_back(' ');
                    lastWasSpace = true;
                }
                continue;
            }
            result.push_back(static_cast<char>(std::toupper(ch)));
            lastWasSpace = false;
        }
        return result;
    }

    bool advanceStartupScreen() {
        if (state_ == ScreenState::ActivisionSplash || state_ == ScreenState::IntroText) {
            changeState(ScreenState::Title);
            return true;
        }
        if (state_ == ScreenState::Title) {
            changeState(ScreenState::Authorization);
            return true;
        }
        if (state_ == ScreenState::Authorization) {
            changeState(ScreenState::CampaignMessage);
            return true;
        }
        if (state_ == ScreenState::CampaignMessage) {
            if (isStorySceneActive()) {
                return false;
            }
            changeState(ScreenState::MainMenu);
            return true;
        }
        return false;
    }

    void handleKey(WPARAM key) {
        if (key == VK_ESCAPE) {
            if (state_ == ScreenState::CampaignMessage && isStorySceneActive()) {
                advanceStoryScene();
                return;
            }
            if (state_ == ScreenState::SystemMenu) {
                returnToMainMenu();
            } else if (state_ == ScreenState::StatusMenu) {
                returnToMainMenu();
            } else if (state_ == ScreenState::NewsNet) {
                changeState(ScreenState::StatusMenu);
            } else if (state_ == ScreenState::ContractMenu) {
                returnToMainMenu();
            } else if (state_ == ScreenState::ContractNegotiation) {
                changeState(ScreenState::ContractMenu);
            } else if (state_ == ScreenState::CrewMenu) {
                if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                    closeCrewAssignment();
                } else {
                    changeState(ScreenState::StatusMenu);
                }
            } else if (state_ == ScreenState::MechLabMenu) {
                returnToMainMenu();
            } else if (state_ == ScreenState::MechExtraAmmo) {
                changeState(ScreenState::MechLabMenu);
            } else if (state_ == ScreenState::MechReviewList) {
                changeState(ScreenState::MechLabMenu);
            } else if (state_ == ScreenState::MechStatus) {
                changeState(ScreenState::MechReviewList);
            } else if (state_ == ScreenState::MechRepairStatus) {
                changeState(ScreenState::MechStatus);
            } else if (state_ == ScreenState::MechSellOffer) {
                mechStatusMenuIndex_ = 3;
                changeState(ScreenState::MechStatus);
            } else if (state_ == ScreenState::MechBuyList) {
                mechLabMenuIndex_ = 2;
                changeState(ScreenState::MechLabMenu);
            } else if (state_ == ScreenState::MechBuyStatus) {
                changeState(ScreenState::MechBuyList);
            } else if (state_ == ScreenState::MechBuyDamageStatus) {
                changeState(ScreenState::MechBuyStatus);
            } else if (state_ == ScreenState::MechBuyCannotAfford ||
                       state_ == ScreenState::MechBuyTooMany) {
                changeState(ScreenState::MechBuyStatus);
            } else if (state_ == ScreenState::BarMenu) {
                if (barDialogState_ != BarDialogState::None) {
                    closeBarDialog();
                } else {
                    returnToMainMenu();
                }
            } else if (state_ == ScreenState::Starmap) {
                if (starmapNameInputActive_) {
                    cancelStarmapNameInput();
                    return;
                }
                if (starmapMenuMode_ == StarmapMenuMode::HousePlanets) {
                    openStarmapHouseMenu();
                    return;
                }
                if (starmapMenuMode_ == StarmapMenuMode::Houses) {
                    closeStarmapMenu();
                    return;
                }
                returnToMainMenu();
            } else if (state_ == ScreenState::TravelRoutePreview) {
                return;
            } else if (state_ == ScreenState::TravelAnimation) {
                return;
            } else if (state_ == ScreenState::MainMenu) {
                if (contractAccepted_) {
                    return;
                }
                systemMenuIndex_ = kSystemSaveMenuIndex;
                changeState(ScreenState::SystemMenu);
            } else if (state_ == ScreenState::SaveGameNameInput) {
                systemMenuIndex_ = kSystemSaveMenuIndex;
                changeState(ScreenState::SystemMenu);
            } else if (state_ == ScreenState::RestoreGameList) {
                systemMenuIndex_ = kSystemRestoreMenuIndex;
                changeState(ScreenState::SystemMenu);
            } else if (state_ == ScreenState::MissionBattleStub) {
                return;
            } else if (state_ == ScreenState::MissionDebrief) {
                if (missionDebriefOutcome_ != MissionOutcome::Death) {
                    changeState(ScreenState::MainMenu);
                }
            } else {
                DestroyWindow(hwnd_);
            }
            return;
        }

        if (state_ == ScreenState::CampaignMessage && isStorySceneActive()) {
            if (key == VK_UP || key == VK_LEFT) {
                moveStoryChoice(-1);
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                moveStoryChoice(1);
            } else if (key == VK_RETURN || key == VK_SPACE) {
                advanceStoryScene();
            }
            return;
        }

        if (state_ == ScreenState::SaveGameNameInput) {
            if (key == VK_RETURN) {
                confirmSaveGameName();
            } else if (key == VK_BACK) {
                if (!saveGameNameInput_.empty()) {
                    saveGameNameInput_.pop_back();
                }
            }
            return;
        }

        if (state_ == ScreenState::RestoreGameList) {
            if (key == VK_UP || key == VK_LEFT) {
                restoreGameSelectionIndex_ =
                    (restoreGameSelectionIndex_ + kRestoreGameCancelIndex) % (kRestoreGameCancelIndex + 1u);
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                restoreGameSelectionIndex_ =
                    (restoreGameSelectionIndex_ + 1u) % (kRestoreGameCancelIndex + 1u);
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateRestoreGameSelection();
            }
            return;
        }

        if (state_ == ScreenState::CrewMenu) {
            if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                if (key == VK_UP || key == VK_LEFT) {
                    moveCrewAssignment(-1);
                } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                    moveCrewAssignment(1);
                } else if (key == VK_RETURN || key == VK_SPACE) {
                    applyCrewAssignmentOption();
                }
            } else if (key == VK_UP || key == VK_LEFT) {
                moveCrewSelection(-1);
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                moveCrewSelection(1);
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateCrewSelection();
            }
            return;
        }

        if (state_ == ScreenState::BarMenu && barDialogState_ != BarDialogState::None) {
            if (barDialogState_ == BarDialogState::RecruitOffer) {
                if (key == VK_UP || key == VK_LEFT) {
                    recruitChoiceIndex_ = (recruitChoiceIndex_ + 1) % 2;
                } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                    recruitChoiceIndex_ = (recruitChoiceIndex_ + 1) % 2;
                } else if (key == VK_RETURN || key == VK_SPACE) {
                    activateRecruitChoice();
                }
            } else if (key == VK_RETURN || key == VK_SPACE) {
                closeBarDialog();
            }
            return;
        }

        if (state_ == ScreenState::Starmap) {
            handleStarmapKey(key);
            return;
        }

        if (key == VK_SPACE) {
            if (!advanceStartupScreen() && state_ == ScreenState::MainMenu) {
                activatePlanetIcon(planetMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::MainMenu) {
            if (!planetIconAvailable(planetMenuIndex_)) {
                planetMenuIndex_ = kPlanetStatusIconIndex;
            }
            if (key == VK_UP || key == VK_LEFT) {
                planetMenuIndex_ = nextAvailablePlanetIcon(planetMenuIndex_, -1);
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                planetMenuIndex_ = nextAvailablePlanetIcon(planetMenuIndex_, 1);
            } else if (key == VK_RETURN) {
                activatePlanetIcon(planetMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::StatusMenu) {
            if (key == VK_UP || key == VK_LEFT) {
                statusMenuIndex_ = (statusMenuIndex_ + kStatusMenuItems.size() - 1) % kStatusMenuItems.size();
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                statusMenuIndex_ = (statusMenuIndex_ + 1) % kStatusMenuItems.size();
            } else if (key == VK_RETURN) {
                activateStatusMenuItem(statusMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::NewsNet) {
            if (key == VK_LEFT || key == VK_UP) {
                newsNetButtonIndex_ = (newsNetButtonIndex_ + 2) % 3;
            } else if (key == VK_RIGHT || key == VK_DOWN || key == VK_TAB) {
                newsNetButtonIndex_ = (newsNetButtonIndex_ + 1) % 3;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateNewsNetButton(newsNetButtonIndex_);
            }
            return;
        }

        if (state_ == ScreenState::ContractMenu) {
            if (key == VK_UP || key == VK_LEFT || key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                contractMenuIndex_ = (contractMenuIndex_ + 1u) % 2u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateContractMenuItem(contractMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::ContractNegotiation) {
            if (contractNegotiationTerminated_ || contractNegotiationUnavailable_) {
                if (key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE) {
                    changeState(ScreenState::ContractMenu);
                }
                return;
            }

            if (contractEditableField_ != ContractEditableField::None) {
                if (key == VK_UP || key == VK_RIGHT) {
                    adjustSelectedContractTerm(1);
                } else if (key == VK_DOWN || key == VK_LEFT) {
                    adjustSelectedContractTerm(-1);
                } else if (key == VK_RETURN || key == VK_SPACE) {
                    contractEditableField_ = ContractEditableField::None;
                } else if (key == VK_TAB) {
                    cycleContractEditableField();
                }
                return;
            }

            if (key == VK_LEFT || key == VK_UP) {
                contractNegotiationButtonIndex_ = (contractNegotiationButtonIndex_ + 2u) % 3u;
            } else if (key == VK_RIGHT || key == VK_DOWN || key == VK_TAB) {
                contractNegotiationButtonIndex_ = (contractNegotiationButtonIndex_ + 1u) % 3u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateContractNegotiationButton(contractNegotiationButtonIndex_);
            }
            return;
        }

        if (state_ == ScreenState::MechLabMenu) {
            if (key == VK_UP || key == VK_LEFT) {
                mechLabMenuIndex_ = (mechLabMenuIndex_ + kMechLabMenuItems.size() - 1) % kMechLabMenuItems.size();
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                mechLabMenuIndex_ = (mechLabMenuIndex_ + 1) % kMechLabMenuItems.size();
            } else if (key == VK_RETURN) {
                activateMechLabMenuItem(mechLabMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::MechExtraAmmo) {
            const size_t count = kAmmoDefinitions.size() + 1u;
            if (key == VK_UP || key == VK_LEFT) {
                extraAmmoSelectionIndex_ = (extraAmmoSelectionIndex_ + count - 1u) % count;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                extraAmmoSelectionIndex_ = (extraAmmoSelectionIndex_ + 1u) % count;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateExtraAmmoSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechReviewList) {
            const size_t count = ownedMechs_.size() + 1u;
            if (key == VK_UP || key == VK_LEFT) {
                selectedMechIndex_ = (selectedMechIndex_ + count - 1u) % count;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                selectedMechIndex_ = (selectedMechIndex_ + 1u) % count;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechReviewSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechStatus) {
            if (key == VK_UP || key == VK_LEFT) {
                mechStatusMenuIndex_ = (mechStatusMenuIndex_ + 4u) % 5u;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                mechStatusMenuIndex_ = (mechStatusMenuIndex_ + 1u) % 5u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechStatusSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechRepairStatus) {
            const OwnedMech* mech = selectedOwnedMech();
            const size_t count = mech ? repairableTargets(*mech).size() : 0;
            if ((key == VK_UP || key == VK_LEFT) && count > 0) {
                repairSelectionIndex_ = (repairSelectionIndex_ + count - 1u) % count;
            } else if ((key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) && count > 0) {
                repairSelectionIndex_ = (repairSelectionIndex_ + 1u) % count;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                repairSelectedMechTarget();
            }
            return;
        }

        if (state_ == ScreenState::MechReloadPrompt) {
            if (key == VK_UP || key == VK_LEFT || key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                reloadSelectionIndex_ = (reloadSelectionIndex_ + 1u) % 2u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechReloadSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechSellOffer) {
            if (key == VK_UP || key == VK_LEFT || key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                sellOfferSelectionIndex_ = (sellOfferSelectionIndex_ + 1u) % 2u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechSellOfferSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechBuyList) {
            const size_t count = currentPlanetMechMarket().mechsForSale.size() + 1u;
            if (key == VK_UP || key == VK_LEFT) {
                selectedMarketMechIndex_ = (selectedMarketMechIndex_ + count - 1u) % count;
                clampMechMarketScroll();
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                selectedMarketMechIndex_ = (selectedMarketMechIndex_ + 1u) % count;
                clampMechMarketScroll();
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechBuyListSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechBuyStatus) {
            if (key == VK_UP || key == VK_LEFT) {
                mechBuyMenuIndex_ = (mechBuyMenuIndex_ + 2u) % 3u;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                mechBuyMenuIndex_ = (mechBuyMenuIndex_ + 1u) % 3u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateMechBuyStatusSelection();
            }
            return;
        }

        if (state_ == ScreenState::MechBuyDamageStatus) {
            if (key == VK_RETURN || key == VK_SPACE) {
                changeState(ScreenState::MechBuyStatus);
            }
            return;
        }

        if (state_ == ScreenState::MechBuyCannotAfford || state_ == ScreenState::MechBuyTooMany) {
            if (key == VK_RETURN || key == VK_SPACE) {
                changeState(ScreenState::MechBuyStatus);
            }
            return;
        }

        if (state_ == ScreenState::BarMenu) {
            if (key == VK_UP || key == VK_LEFT) {
                barMenuIndex_ = (barMenuIndex_ + kBarMenuItems.size() - 1) % kBarMenuItems.size();
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                barMenuIndex_ = (barMenuIndex_ + 1) % kBarMenuItems.size();
            } else if (key == VK_RETURN) {
                activateBarMenuItem(barMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::SystemMenu) {
            if (key == VK_UP || key == VK_LEFT) {
                systemMenuIndex_ = (systemMenuIndex_ + kSystemMenuItems.size() - 1) % kSystemMenuItems.size();
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                systemMenuIndex_ = (systemMenuIndex_ + 1) % kSystemMenuItems.size();
            } else if (key == VK_RETURN) {
                activateSystemMenuItem(systemMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::MissionBattleStub) {
            if (key == VK_UP || key == VK_LEFT) {
                battleStubButtonIndex_ = (battleStubButtonIndex_ + 2u) % 3u;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                battleStubButtonIndex_ = (battleStubButtonIndex_ + 1u) % 3u;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                activateBattleStubSelection();
            }
            return;
        }

        if (state_ == ScreenState::MissionDebrief) {
            if (missionDebriefOutcome_ == MissionOutcome::Death) {
                if (key == VK_UP || key == VK_LEFT || key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                    missionDeathMenuIndex_ = (missionDeathMenuIndex_ + 1u) % 2u;
                } else if (key == VK_RETURN || key == VK_SPACE) {
                    if (missionDeathMenuIndex_ == 0) {
                        restartCampaign();
                    } else {
                        DestroyWindow(hwnd_);
                    }
                }
            } else if (key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE) {
                changeState(ScreenState::MainMenu);
            }
            return;
        }

        if (key == 'R') {
            loadResources();
            changeState(ScreenState::ActivisionSplash);
        }
    }

    void handleChar(WPARAM character) {
        if (state_ == ScreenState::SaveGameNameInput) {
            const wchar_t ch = static_cast<wchar_t>(character);
            if (ch == L'\r' || ch == L'\n' || ch == L'\b' || ch == 27) {
                return;
            }
            if (saveGameNameInput_.size() >= kGamMaxNameChars || ch < 32 || ch >= 128) {
                return;
            }

            const char narrow = static_cast<char>(ch);
            if (std::isalnum(static_cast<unsigned char>(narrow)) ||
                narrow == '_' ||
                narrow == '-' ||
                narrow == '$') {
                saveGameNameInput_.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(narrow))));
            }
            return;
        }

        if (state_ != ScreenState::Starmap || !starmapNameInputActive_) {
            return;
        }

        const wchar_t ch = static_cast<wchar_t>(character);
        if (ch == L'\r' || ch == L'\n' || ch == L'\b' || ch == 27) {
            return;
        }
        if (ch < 32 || ch >= 128 || starmapNameInput_.size() >= kStarmapPlanetNameInputMaxChars) {
            return;
        }

        const char narrow = static_cast<char>(ch);
        if (std::isalnum(static_cast<unsigned char>(narrow)) ||
            narrow == ' ' ||
            narrow == '\'' ||
            narrow == '-' ||
            narrow == '.') {
            starmapNameInput_.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(narrow))));
        }
    }

    void loadResources() {
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugLog(L"Loading game resources from: " + resourceRoot_.wstring());
#endif
        status_.clear();
        archive_ = {};
        activisionArchive_ = {};
        titleArchive_ = {};
        gpicsArchive_ = {};
        campaignArchive_ = {};
        barArchive_ = {};
        barEnvironmentOverlayArchive_ = {};
        barInformationArchive_ = {};
        crewArchive_ = {};
        crewMechArchive_ = {};
        mechStatusArchive_ = {};
        houseEmblemsArchive_ = {};
        contractHouseNamesArchive_ = {};
        contractHouseEmblemsArchive_ = {};
        contractContactPortraitsArchive_ = {};
        missionResultArchive_ = {};
        travelShuttleArchive_ = {};
        travelEngineArchive_ = {};
        mwMainData_.clear();
        font_ = {};
        smallFont_ = {};
        menuFont_ = {};
        campaignMessageLines_.clear();
        newsNetMessageLines_.clear();
        storyPages_.clear();
        currentStoryPageIndex_ = 0;
        currentStoryChoiceIndex_ = 0;
        pendingStoryDefaultAction_ = StoryAction::None;
        pendingStoryReturnState_ = ScreenState::MainMenu;
        storyBackdrop_ = StoryBackdrop::Campaign;
        storyMessageFlags_ = {};
        startingStoryPlanetName_.clear();
        grigDestinationPlanet_.clear();
        wendallDestinationPlanet_.clear();
        kearneyDestinationPlanet_.clear();
        blackWidowDestinationPlanet_.clear();
        darkWingDestinationPlanet_.clear();
        newsNetNoOtherLines_.clear();
        missionVictoryLines_.clear();
        missionDefeatLines_.clear();
        missionDeathPromptLines_.clear();
        activeNewsNetMessageIndexes_.clear();
        activeContracts_.clear();
        acceptedContract_ = {};
        contractAccepted_ = false;
        missionLaunchPending_ = false;
        finalMissionStubActive_ = false;
        missionParticipants_.clear();
        missionDebriefOutcome_ = MissionOutcome::Victory;
        missionDebriefSalvage_ = 0;
        missionDebriefPayment_ = 0;
        recruitPilots_.clear();
        planetRecruitPools_.clear();
        contractNegotiationLockedVisitByPlanet_.clear();
        recruitLastPlanetIndex_.clear();
        recruitLastMonthKey_.clear();
        previousPlanetRecruitIndexes_.clear();
        planetMechMarkets_.clear();
        planets_.clear();
        barDialogState_ = BarDialogState::None;
        activeRecruitIndex_ = -1;
        recruitChoiceIndex_ = 0;
        saveGameNameInput_.clear();
        restoreGameSlots_.assign(kGamVisibleSlotCount, {});
        restoreGameSelectionIndex_ = 0;
        gamRawState_.clear();
        gamRawStateValid_ = false;
        starmapNameInputActive_ = false;
        starmapNameInput_.clear();
        resetReputationAndHouseStanding();
        resetCrewRoster();
        try {
            const fs::path mwMainPath = resourceRoot_ / L"MW_MAIN.EXE";
            mwMainData_ = readFile(mwMainPath);
            archive_ = loadPicsArchive(resourceRoot_ / L"MW_1PICS.BIN");
            activisionArchive_ = loadPicsArchive(resourceRoot_ / L"MW_APICS.BIN");
            titleArchive_ = loadPicsArchive(resourceRoot_ / L"MW_TPICS.BIN");
            gpicsArchive_ = loadPicsArchive(resourceRoot_ / L"MW_GPICS.BIN");
            crewArchive_ = loadPicsArchive(resourceRoot_ / L"MW_CPICS.BIN");
            const fs::path mwPicsPath = resourceRoot_ / L"MW_PICS.BIN";
            const auto appendImages = [](PicsArchive& destination, PicsArchive source) {
                destination.decodedSize = std::max(destination.decodedSize, source.decodedSize);
                if (destination.palette.empty()) {
                    destination.palette = source.palette;
                }
                destination.images.insert(
                    destination.images.end(),
                    std::make_move_iterator(source.images.begin()),
                    std::make_move_iterator(source.images.end()));
            };
            campaignArchive_ = loadRawPicsImage(mwPicsPath, 0x00000280u, kCampaignDesertEntry);
            appendImages(campaignArchive_, loadRawPicsImage(mwPicsPath, 0x00007F89u, kCampaignTropicalEntry));
            appendImages(campaignArchive_, loadRawPicsImage(mwPicsPath, 0x0000FC92u, kCampaignIceEntry));
            loadMechArtArchives();
            travelShuttleArchive_ = loadRawPicsImage(mwPicsPath, 0x000680D5u, kTravelShuttleEntry);
            travelEngineArchive_ = loadPicsArchive(resourceRoot_ / L"MW_2PICS.BIN");
            barArchive_ = loadRawPicsImageWithNibblePhase(
                mwPicsPath,
                0x0001799Bu,
                kBarBaseEntry,
                kScreenWidth,
                kScreenHeight,
                true,
                1);
            barEnvironmentOverlayArchive_ = loadRawPicsImageWithNibblePhase(
                mwPicsPath,
                0x000273AEu,
                kBarTropicalOverlayEntry,
                161,
                70,
                true,
                1);
            appendImages(
                barEnvironmentOverlayArchive_,
                loadRawPicsImage(mwPicsPath, 0x000289DEu, kBarIceOverlayEntry));
            barInformationArchive_ = loadRawPicsImage(mwPicsPath, 0x0001F6A5u, 4);
            smallFont_ = loadFont(resourceRoot_ / L"6X6.FNT");
            font_ = loadFont(resourceRoot_ / L"8X8B.FNT");
            menuFont_ = loadFont(resourceRoot_ / L"FOX88.FNT");
            campaignMessageLines_ = parseMwMainTextBlock(mwMainData_, kCampaignIntroText);
            newsNetNoOtherLines_ = parseMwMainTextBlock(mwMainData_, kNewsNetNoOtherText);
            missionVictoryLines_ = parseMwMainTextBlock(mwMainData_, kMissionVictoryText);
            missionDefeatLines_ = parseMwMainTextBlock(mwMainData_, kMissionDefeatText);
            missionDeathPromptLines_ = parseMwMainTextBlock(mwMainData_, kMissionDeathPromptText);
            newsNetMessageLines_.reserve(kNewsNetEntries.size() + kStoryNewsNetEntries.size());
            for (const NewsNetEntry& entry : kNewsNetEntries) {
                newsNetMessageLines_.push_back(parseMwMainTextBlock(mwMainData_, entry.text));
            }
            for (const StoryNewsNetEntry& entry : kStoryNewsNetEntries) {
                std::vector<std::wstring> lines = storyLines(entry.text);
                if (entry.messageId == 0x36) {
                    appendToLastStoryLine(lines, blackWidowDestinationPlanet());
                    appendStoryLines(lines, storyLines(kStoryNewsBlackWidowLeadSuffix));
                } else if (entry.messageId == 0x40 && !lines.empty()) {
                    if (lines.front().size() >= 2 && lines.front()[0] == L'-' && lines.front()[1] == L' ') {
                        lines.front().erase(0, 2);
                    }
                }
                newsNetMessageLines_.push_back(std::move(lines));
            }
            recruitPilots_ = loadRecruitPilots(mwMainData_);
            planets_ = loadPlanetRecords(mwMainPath);
            planetMechMarkets_.assign(planets_.size(), {});
            currentPlanetIndex_ = chooseNewGameStartingPlanetIndex();
            startingStoryPlanetName_ = currentPlanetNameAscii();
            selectedPlanetIndex_ = currentPlanetIndex_;
            pendingTravelPlanetIndex_ = currentPlanetIndex_;
            initializeRecruitmentState();
            loadHouseEmblems();
            loadContractMenuImages();
            loadMissionResultImages();
            std::wstringstream stream;
            stream << L"Loaded PICS: MW_1=" << archive_.images.size()
                   << L", MW_A=" << activisionArchive_.images.size()
                   << L", MW_T=" << titleArchive_.images.size()
                   << L", MW_G=" << gpicsArchive_.images.size()
                   << L", MW_C=" << crewArchive_.images.size()
                   << L", MW_PICS raw=" << campaignArchive_.images.size() + barArchive_.images.size() + barEnvironmentOverlayArchive_.images.size() + barInformationArchive_.images.size() + crewMechArchive_.images.size() + mechStatusArchive_.images.size() + travelShuttleArchive_.images.size() + contractHouseNamesArchive_.images.size() + contractHouseEmblemsArchive_.images.size() + contractContactPortraitsArchive_.images.size() + missionResultArchive_.images.size()
                   << L", planets=" << planets_.size()
                   << L", pilots=" << recruitPilots_.size()
                   << L", text=" << widen(kCampaignIntroText.id);
            status_ = stream.str();
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            debugLog(status_);
#endif
        } catch (const std::exception& error) {
            std::wstringstream stream;
            stream << L"Resource load failed: " << widen(error.what())
                   << L" | root: " << resourceRoot_.wstring();
            status_ = stream.str();
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
            debugLog(status_);
#endif
        }
    }

    void loadHouseEmblems() {
        houseEmblemsArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        const fs::path picsPath = resourceRoot_ / L"MW_PICS.BIN";
        const auto append = [&](PicsArchive source) {
            houseEmblemsArchive_.decodedSize = std::max(houseEmblemsArchive_.decodedSize, source.decodedSize);
            houseEmblemsArchive_.images.insert(
                houseEmblemsArchive_.images.end(),
                std::make_move_iterator(source.images.begin()),
                std::make_move_iterator(source.images.end()));
        };

        try {
            append(loadRawPicsImage(picsPath, 0x00046783u, 0));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImage(picsPath, 0x00046DFEu, 1));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImageWithNibblePhase(picsPath, 0x00047479u, 2, 60, 55, true, 1));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImage(picsPath, 0x00047AF5u, 3));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImageWithNibblePhase(picsPath, 0x00048170u, 4, 60, 55, true, 1));
        } catch (const std::exception&) {
        }
    }

    void loadContractMenuImages() {
        contractHouseNamesArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        contractHouseEmblemsArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        contractContactPortraitsArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));

        const fs::path picsPath = resourceRoot_ / L"MW_PICS.BIN";
        for (size_t i = 0; i < kContractHouseNameImages.size(); ++i) {
            try {
                appendContractImage(contractHouseNamesArchive_, picsPath, kContractHouseNameImages[i], static_cast<int>(i));
            } catch (const std::exception&) {
            }
        }
        for (size_t i = 0; i < kContractHouseEmblemImages.size(); ++i) {
            try {
                appendContractImage(contractHouseEmblemsArchive_, picsPath, kContractHouseEmblemImages[i], static_cast<int>(i));
            } catch (const std::exception&) {
            }
        }
        for (size_t house = 0; house < kContractContactPortraitImages.size(); ++house) {
            const size_t portraitCount = kContractContactPortraitCounts[house];
            for (size_t slot = 0; slot < portraitCount; ++slot) {
                try {
                    appendContractImage(
                        contractContactPortraitsArchive_,
                        picsPath,
                        kContractContactPortraitImages[house][slot],
                        contractContactPortraitEntryIndex(house, slot));
                } catch (const std::exception&) {
                }
            }
        }
    }

    void loadMissionResultImages() {
        missionResultArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        const fs::path picsPath = resourceRoot_ / L"MW_PICS.BIN";
        const auto append = [&](PicsArchive source) {
            missionResultArchive_.decodedSize = std::max(missionResultArchive_.decodedSize, source.decodedSize);
            missionResultArchive_.images.insert(
                missionResultArchive_.images.end(),
                std::make_move_iterator(source.images.begin()),
                std::make_move_iterator(source.images.end()));
        };

        try {
            append(loadRawPicsImageWithNibblePhase(
                picsPath,
                0x00065B96u,
                kMissionResultDeathImageEntry,
                149,
                127,
                true,
                1));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImage(picsPath, 0x0006FDF4u, kMissionResultVictoryImageEntry));
        } catch (const std::exception&) {
        }
        try {
            append(loadRawPicsImage(picsPath, 0x000776FCu, kMissionResultDefeatImageEntry));
        } catch (const std::exception&) {
        }
    }

    static void appendContractImage(
        PicsArchive& destination,
        const fs::path& picsPath,
        const ContractImageSpec& spec,
        int entryIndex) {
        PicsArchive source = spec.nibblePhase == 0
            ? loadRawPicsImage(picsPath, spec.offset, entryIndex)
            : loadRawPicsImageWithNibblePhase(
                  picsPath,
                  spec.offset,
                  entryIndex,
                  spec.width,
                  spec.height,
                  true,
                  spec.nibblePhase);
        destination.decodedSize = std::max(destination.decodedSize, source.decodedSize);
        destination.images.insert(
            destination.images.end(),
            std::make_move_iterator(source.images.begin()),
            std::make_move_iterator(source.images.end()));
    }

    void loadMechArtArchives() {
        crewMechArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        mechStatusArchive_.palette.assign(std::begin(kEgaPalette), std::end(kEgaPalette));
        const fs::path picsPath = resourceRoot_ / L"MW_PICS.BIN";

        const auto append = [](PicsArchive& destination, PicsArchive source) {
            destination.decodedSize = std::max(destination.decodedSize, source.decodedSize);
            destination.images.insert(
                destination.images.end(),
                std::make_move_iterator(source.images.begin()),
                std::make_move_iterator(source.images.end()));
        };

        for (const MechDefinition& definition : kMechDefinitions) {
            append(
                crewMechArchive_,
                loadRawPicsImageWithNibblePhase(
                    picsPath,
                    definition.crewImage.offset,
                    definition.crewImage.entry,
                    definition.crewImage.width,
                    definition.crewImage.height,
                    true,
                    definition.crewImage.nibblePhase));
            append(
                mechStatusArchive_,
                loadRawPicsImageWithNibblePhase(
                    picsPath,
                    definition.statusImage.offset,
                    definition.statusImage.entry,
                    definition.statusImage.width,
                    definition.statusImage.height,
                    true,
                    definition.statusImage.nibblePhase));
        }
    }

    void resetCrewRoster() {
        crewMembers_ = {{
            {true, L"G BRAVER", L"POOR", L"POOR", 0, kCrewPlayerPortraitEntry, -1},
            {},
            {},
            {},
        }};
        ownedMechs_ = {{makeStartingJenner()}};
        randomizeStartingMechDamage(ownedMechs_.front());
        crewInteractionMode_ = CrewInteractionMode::Navigate;
        crewSelectionIndex_ = kCrewDoneSelectionIndex;
        crewAssignmentIndex_ = 0;
    }

    void resetReputationAndHouseStanding() {
        playerReputationPoints_ = 0;
        playerReputationTier_ = 0;
        housePositiveCounters_.fill(0);
        houseNegativeCounters_.fill(0);
        syncFamilyAttitudesFromHouseCounters();
    }

    void initializeRecruitmentState() {
        planetRecruitPools_.assign(planets_.size(), {});
        contractNegotiationLockedVisitByPlanet_.assign(planets_.size(), 0);
        recruitLastPlanetIndex_.assign(recruitPilots_.size(), -1);
        recruitLastMonthKey_.assign(recruitPilots_.size(), -1000000);
        previousPlanetRecruitIndexes_.clear();
        recruitmentSeed_ = makeRecruitmentSeed();
        planetVisitSerialCounter_ = 1;
        currentPlanetVisitSerial_ = planetVisitSerialCounter_;
    }

    static uint32_t makeRecruitmentSeed() {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        uint64_t value = static_cast<uint64_t>(counter.QuadPart);
        value ^= static_cast<uint64_t>(GetTickCount()) << 32;
        value ^= static_cast<uint64_t>(GetCurrentProcessId()) * 0x9E3779B97F4A7C15ull;
        value ^= value >> 33;
        value *= 0xFF51AFD7ED558CCDull;
        value ^= value >> 33;
        return static_cast<uint32_t>(value ^ (value >> 32));
    }

    std::vector<RecruitPilot> loadRecruitPilots(const std::vector<uint8_t>& mwMainData) const {
        std::vector<RecruitPilot> pilots;
        pilots.reserve(kRecruitPilotDefinitions.size());

        for (const PilotTextDefinition& definition : kRecruitPilotDefinitions) {
            if (definition.name.fileOffset < 3 || definition.name.fileOffset >= mwMainData.size()) {
                throw std::runtime_error("MW_MAIN.EXE pilot metadata is out of range: " + std::string(definition.id));
            }

            RecruitPilot pilot;
            pilot.id = definition.id;
            pilot.portraitEntry = mwMainData[definition.name.fileOffset - 3u];
            pilot.gunnerySkill = mwMainData[definition.name.fileOffset - 2u];
            pilot.pilotingSkill = mwMainData[definition.name.fileOffset - 1u];
            if (pilot.portraitEntry < 1 || pilot.portraitEntry > 21 ||
                pilot.gunnerySkill > 3 ||
                pilot.pilotingSkill > 3) {
                throw std::runtime_error("MW_MAIN.EXE pilot metadata was not recognized: " + std::string(definition.id));
            }

            const std::vector<std::wstring> nameLines = parseMwMainTextBlock(mwMainData, definition.name);
            if (nameLines.empty()) {
                throw std::runtime_error("MW_MAIN.EXE pilot name is empty: " + std::string(definition.id));
            }
            pilot.name = trimRightSpaces(nameLines.front());
            pilot.quoteLines = parseMwMainTextBlock(mwMainData, definition.quote);
            pilot.monthlyWage = monthlyWageForGunnery(pilot.gunnerySkill);
            pilots.push_back(std::move(pilot));
        }

        return pilots;
    }

    static std::wstring trimRightSpaces(std::wstring text) {
        while (!text.empty() && text.back() == L' ') {
            text.pop_back();
        }
        return text;
    }

    static uint32_t monthlyWageForGunnery(uint8_t gunnerySkill) {
        static constexpr std::array<uint32_t, 4> kWagesByGunnery = {{
            400,
            600,
            1000,
            2000,
        }};
        return kWagesByGunnery[std::min<size_t>(gunnerySkill, kWagesByGunnery.size() - 1u)];
    }

    static std::wstring_view skillLabel(uint8_t skill) {
        static constexpr std::array<std::wstring_view, 4> kSkillLabels = {{
            L"POOR",
            L"AVERAGE",
            L"GOOD",
            L"EXCELLENT",
        }};
        return kSkillLabels[std::min<size_t>(skill, kSkillLabels.size() - 1u)];
    }

    void update() {
        const DWORD now = GetTickCount();
        const DWORD elapsed = now - stateStartedTick_;
        if (state_ == ScreenState::ActivisionSplash && elapsed > 2200) {
            changeState(ScreenState::IntroText);
        } else if (state_ == ScreenState::IntroText && elapsed > 6500) {
            changeState(ScreenState::Title);
        } else if (state_ == ScreenState::Authorization && elapsed > 1200) {
            changeState(ScreenState::CampaignMessage);
        } else if (state_ == ScreenState::ContractAcceptedMessage && elapsed > kContractAcceptedMessageMs) {
            changeState(ScreenState::MainMenu);
        } else if (state_ == ScreenState::MechLabMenu) {
            updateMechLabAnimation(now);
        } else if (state_ == ScreenState::TravelRoutePreview && elapsed > kTravelRoutePreviewMs) {
            changeState(ScreenState::TravelAnimation);
        } else if (state_ == ScreenState::TravelAnimation) {
            updateTravelAnimation(elapsed);
        }
    }

    void updateTravelAnimation(DWORD elapsed) {
        const DWORD animationMs =
            kTravelEngineDelayMs +
            static_cast<DWORD>(kTravelEngineSequence.size()) * kTravelEngineFrameMs +
            kTravelEngineFinalHoldMs;
        if (elapsed < animationMs) {
            return;
        }

        if (missionLaunchPending_) {
            missionLaunchPending_ = false;
            battleStubButtonIndex_ = 0;
            changeState(ScreenState::MissionBattleStub);
            return;
        }

        completeTravel();
        if (!tryStartPostTravelStory()) {
            changeState(ScreenState::MainMenu);
        }
    }

    void completeTravel() {
        advanceCampaignDays(pendingTravelDays_);
        if (!planets_.empty()) {
            previousPlanetRecruitIndexes_ = currentPlanetGeneratedRecruitIndexes();
            currentPlanetIndex_ = std::clamp(pendingTravelPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
            selectedPlanetIndex_ = currentPlanetIndex_;
            currentPlanetVisitSerial_ = ++planetVisitSerialCounter_;
            barDialogState_ = BarDialogState::None;
            activeRecruitIndex_ = -1;
        }
        playerWealth_ = (playerWealth_ > pendingTravelCost_) ? (playerWealth_ - pendingTravelCost_) : 0;
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        debugLog(
            L"Travel completed. Planet: " +
            currentPlanetName() +
            L", wealth=" +
            formatWealth(playerWealth_) +
            L", date=" +
            campaignDateLabel());
#endif
        pendingTravelCost_ = 0;
        pendingTravelDays_ = 0;
    }

    std::vector<size_t> currentPlanetGeneratedRecruitIndexes() const {
        if (currentPlanetIndex_ < 0 || static_cast<size_t>(currentPlanetIndex_) >= planetRecruitPools_.size()) {
            return {};
        }
        return planetRecruitPools_[static_cast<size_t>(currentPlanetIndex_)].originalCandidates;
    }

    void changeState(ScreenState state) {
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        if (state_ != state) {
            debugLog(
                L"Screen: " +
                std::wstring(screenStateName(state_)) +
                L" -> " +
                std::wstring(screenStateName(state)));
        }
#endif
        state_ = state;
        stateStartedTick_ = GetTickCount();
        if (state_ == ScreenState::MechLabMenu) {
            resetMechLabAnimation(stateStartedTick_);
        } else if (state_ == ScreenState::MainMenu) {
            if (!planetIconAvailable(planetMenuIndex_)) {
                planetMenuIndex_ = kPlanetStatusIconIndex;
            }
        } else if (state_ == ScreenState::Starmap) {
            starmapButtonIndex_ = kStarmapNoButtonSelection;
            closeStarmapMenu();
            starmapNameInputActive_ = false;
            starmapNameInput_.clear();
            if (!planets_.empty()) {
                selectedPlanetIndex_ = std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
            }
        } else if (state_ == ScreenState::ContractMenu) {
            contractMenuIndex_ = std::min<size_t>(contractMenuIndex_, 1u);
        } else if (state_ == ScreenState::ContractNegotiation) {
            contractNegotiationButtonIndex_ = std::min<size_t>(contractNegotiationButtonIndex_, 2u);
            activeContractIndex_ = activeContracts_.empty()
                ? 0
                : std::min(activeContractIndex_, activeContracts_.size() - 1u);
        } else if (state_ == ScreenState::MechExtraAmmo) {
            extraAmmoSelectionIndex_ = std::min<size_t>(extraAmmoSelectionIndex_, kAmmoDefinitions.size());
        } else if (state_ == ScreenState::MechReviewList) {
            selectedMechIndex_ = std::min(selectedMechIndex_, ownedMechs_.size());
        } else if (state_ == ScreenState::MechStatus) {
            selectedMechIndex_ = ownedMechs_.empty() ? 0 : std::min(selectedMechIndex_, ownedMechs_.size() - 1u);
            mechStatusMenuIndex_ = std::min<size_t>(mechStatusMenuIndex_, 4u);
        } else if (state_ == ScreenState::MechReloadPrompt) {
            selectedMechIndex_ = ownedMechs_.empty() ? 0 : std::min(selectedMechIndex_, ownedMechs_.size() - 1u);
            reloadSelectionIndex_ = std::min<size_t>(reloadSelectionIndex_, 1u);
        } else if (state_ == ScreenState::MechSellOffer) {
            selectedMechIndex_ = ownedMechs_.empty() ? 0 : std::min(selectedMechIndex_, ownedMechs_.size() - 1u);
            sellOfferSelectionIndex_ = std::min<size_t>(sellOfferSelectionIndex_, 1u);
        } else if (state_ == ScreenState::MechBuyList) {
            ensureCurrentPlanetMechMarket();
            clampMechMarketScroll();
        } else if (state_ == ScreenState::MechBuyStatus ||
                   state_ == ScreenState::MechBuyCannotAfford ||
                   state_ == ScreenState::MechBuyTooMany) {
            ensureCurrentPlanetMechMarket();
            const PlanetMechMarket& market = currentPlanetMechMarket();
            selectedMarketMechIndex_ = market.mechsForSale.empty()
                ? 0
                : std::min(selectedMarketMechIndex_, market.mechsForSale.size() - 1u);
            mechBuyMenuIndex_ = std::min<size_t>(mechBuyMenuIndex_, 2u);
        } else if (state_ == ScreenState::MechBuyDamageStatus) {
            ensureCurrentPlanetMechMarket();
            const PlanetMechMarket& market = currentPlanetMechMarket();
            selectedMarketMechIndex_ = market.mechsForSale.empty()
                ? 0
                : std::min(selectedMarketMechIndex_, market.mechsForSale.size() - 1u);
        } else if (state_ == ScreenState::CrewMenu) {
            crewInteractionMode_ = CrewInteractionMode::Navigate;
            crewSelectionIndex_ = kCrewDoneSelectionIndex;
            crewAssignmentIndex_ = 0;
        }
    }

    void render() {
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000u);
        if (archive_.images.empty()) {
            drawText(4, 16, L"MW_MAIN replacement engine", 15, 0);
            drawText(4, 32, L"No MW_1PICS.BIN image loaded.", 14, 0);
            drawText(4, 4, status_, 15, 0);
            return;
        }

        switch (state_) {
        case ScreenState::ActivisionSplash:
            drawImageFullScreenOrCentered(activisionArchive_, 1);
            break;
        case ScreenState::IntroText:
            renderIntroText();
            break;
        case ScreenState::Title:
            drawImageFullScreenOrCentered(titleArchive_, 1);
            break;
        case ScreenState::Authorization:
            renderAuthorization();
            break;
        case ScreenState::CampaignMessage:
            renderCampaignMessage();
            break;
        case ScreenState::MainMenu:
            renderMainMenu();
            break;
        case ScreenState::StatusMenu:
            renderStatusMenu();
            break;
        case ScreenState::NewsNet:
            renderNewsNet();
            break;
        case ScreenState::ContractMenu:
            renderContractMenu();
            break;
        case ScreenState::ContractNegotiation:
            renderContractNegotiation();
            break;
        case ScreenState::ContractAcceptedMessage:
            renderContractAcceptedMessage();
            break;
        case ScreenState::MechLabMenu:
            renderMechLabMenu();
            break;
        case ScreenState::MechExtraAmmo:
            renderExtraAmmoMenu();
            break;
        case ScreenState::MechReviewList:
            renderMechReviewList();
            break;
        case ScreenState::MechStatus:
            renderMechStatus();
            break;
        case ScreenState::MechRepairStatus:
            renderMechRepairStatus();
            break;
        case ScreenState::MechReloadPrompt:
            renderMechReloadPrompt();
            break;
        case ScreenState::MechSellOffer:
            renderMechSellOffer();
            break;
        case ScreenState::MechBuyList:
            renderMechBuyList();
            break;
        case ScreenState::MechBuyStatus:
            renderMechBuyStatus();
            break;
        case ScreenState::MechBuyDamageStatus:
            renderMechBuyDamageStatus();
            break;
        case ScreenState::MechBuyCannotAfford:
            renderMechBuyStatus();
            renderMechBuyMessage(L"YOU CAN'T AFFORD THIS!");
            break;
        case ScreenState::MechBuyTooMany:
            renderMechBuyStatus();
            renderMechBuyMessage(L"THE LAW ALLOWS A CITIZEN TO", L"OWN NO MORE THEN 12 MECHS!");
            break;
        case ScreenState::BarMenu:
            renderBarMenu();
            break;
        case ScreenState::SystemMenu:
            renderSystemMenu();
            break;
        case ScreenState::SaveGameNameInput:
            renderSaveGameNameInput();
            break;
        case ScreenState::RestoreGameList:
            renderRestoreGameList();
            break;
        case ScreenState::CrewMenu:
            renderCrewMenu();
            break;
        case ScreenState::Starmap:
            renderStarmap();
            break;
        case ScreenState::TravelRoutePreview:
            renderStarmap();
            break;
        case ScreenState::TravelAnimation:
            renderTravelAnimation();
            break;
        case ScreenState::MissionBattleStub:
            renderBattleStub();
            break;
        case ScreenState::MissionDebrief:
            renderMissionDebrief();
            break;
        }

        if (!status_.empty() && status_.find(L"failed") != std::wstring::npos) {
            drawText(4, 4, status_, 15, 0);
        }
#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
        renderDebugConsoleOverlay();
#endif
    }

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
    void renderDebugConsoleOverlay() {
        if (!debugConsoleOpen_) {
            return;
        }

        constexpr int kConsoleHeight = 72;
        fillRect(0, 0, kScreenWidth, kConsoleHeight, 0);
        drawBox(0, 0, kScreenWidth, kConsoleHeight, 7);
        drawSmallTextPlain(6, 5, L"DEBUG CONSOLE", 15);

        const std::vector<std::wstring>& lines = debugTools_.recentLines();
        const size_t visible = std::min<size_t>(5, lines.size());
        int y = 16;
        for (size_t i = lines.size() - visible; i < lines.size(); ++i) {
            drawDebugConsoleLine(6, y, lines[i]);
            y += 8;
        }

        drawSmallTextPlain(6, 61, L"> " + debugConsoleInput_, 15);
    }

    void drawDebugConsoleLine(int x, int y, std::wstring_view text) {
        constexpr size_t kMaxVisibleChars = 50;
        if (text.size() <= kMaxVisibleChars) {
            drawSmallTextPlain(x, y, text, 7);
            return;
        }
        drawSmallTextPlain(x, y, text.substr(text.size() - kMaxVisibleChars), 7);
    }
#endif

    void renderIntroText() {
        static constexpr std::array<std::wstring_view, 9> lines = {{
            L"BETWEEN THE FALL OF THE ANCIENT STAR",
            L"LEAGUE AND THE RISE OF THE HEIRS OF",
            L"STEINER-DAVION THERE LIVED AN AGE",
            L"OF WAR UNLIKE ANY BEFORE OR SINCE.",
            L"FIVE GREAT HOUSES STRUGGLED FIERCELY",
            L"FOR SOLE POSSESSION OF THE INNER SPHERE",
            L"OF MAN. ACROSS THIS TROUBLED STARSCAPE",
            L"MARCHED MIGHTY MACHINES OF WAR LIKE",
            L"TITANS THROUGH A GREAT STORM OF FIRE.",
        }};
        int y = 43;
        for (std::wstring_view line : lines) {
            drawSmallText(43, y, line, 15, 0);
            y += 13;
        }
        drawSmallText(43, y + 2, L"IT WAS AN AGE OF HIGH ADVENTURE...", 15, 0);
    }

    void renderAuthorization() {
        drawImageFullScreenOrCentered(titleArchive_, 2);
        const DWORD elapsed = GetTickCount() - stateStartedTick_;
        if (elapsed < 700) {
            fillRect(34, 132, 252, 54, 0);
            drawBox(32, 130, 256, 58, 7);
            drawSmallText(42, 138, L"WHAT IS YOUR PASSWORD?", 15, 0);
            drawSmallText(42, 154, L"CODE CHECK: WHISKEY DELTA TANGO", 15, 0);
            drawSmallText(42, 170, L"AUTHORIZATION CODE: ", 15, 0);
        } else {
            fillRect(68, 98, 184, 34, 8);
            drawBox(66, 96, 188, 38, 7);
            drawSmallTextCenteredInRect(66, 106, 188, L"AUTHORIZATION CONFIRMED!", 15, 8);
            drawSmallTextCenteredInRect(66, 120, 188, L"GLAD TO HAVE YOU ABOARD.", 15, 8);
        }
    }

    void renderCampaignShell(bool introMessage = false) {
        renderCampaignBackdrop();
        drawCampaignButtons(introMessage);
    }

    void renderCampaignBackdrop() {
        drawImageFullScreenOrCentered(campaignArchive_, campaignBackdropEntry());
        drawHeaderPanel(10, 10, 145, currentPlanetName());
        drawHeaderPanel(170, 10, 145, campaignDateLabel());
    }

    void renderCampaignMessage() {
        if (isStorySceneActive()) {
            renderStoryScene();
            return;
        }
        renderCampaignShell(true);
        drawGpPanel(22, 64, 276, 76, 8);
        int y = 74;
        for (const std::wstring& line : campaignMessageLines_) {
            drawSmallTextPlain(31, y, line, 7);
            y += 8;
        }
    }

    void renderStoryScene() {
        if (storyBackdrop_ == StoryBackdrop::Bar) {
            renderBarInformationBackdrop();
        } else if (storyBackdrop_ == StoryBackdrop::Contract) {
            renderContractStoryBackdrop();
        } else if (storyBackdrop_ == StoryBackdrop::MechLab) {
            drawMechLabBackground();
        } else {
            renderCampaignBackdrop();
        }

        const StoryPage& page = storyPages_[currentStoryPageIndex_];
        const RectI panel = storyPanelRect(page);
        drawGpPanel(panel.x, panel.y, panel.width, panel.height, 8);

        const int textX = panel.x + 9;
        const int textY = panel.y + 10;
        const int textWidth = panel.width - 18;
        const int lineStep = storyLineStep(page);
        const int textBottom = panel.y + panel.height - 8;

        int y = textY;
        for (size_t i = 0; i < page.lines.size(); ++i) {
            if (y + 5 > textBottom) {
                break;
            }
            uint8_t color = 7;
            std::wstring trimmed = page.lines[i];
            while (!trimmed.empty() && trimmed.front() == L' ') {
                trimmed.erase(trimmed.begin());
            }
            for (size_t choiceIndex = 0; choiceIndex < page.choices.size(); ++choiceIndex) {
                if (trimmed == page.choices[choiceIndex].label) {
                    color = choiceIndex == currentStoryChoiceIndex_ ? 14 : 7;
                    break;
                }
            }
            drawNewsNetText5x5(textX, y, page.lines[i], color, textWidth);
            y += lineStep;
        }
    }

    void renderMainMenu() {
        renderCampaignShell();
    }

    void renderBarInformationBackdrop() {
        if (!barInformationArchive_.images.empty()) {
            drawImageFullScreenOrCentered(barInformationArchive_, 4);
        } else {
            renderBarMenu();
        }
    }

    void renderContractStoryBackdrop() {
        drawNewsNetFrame();
        const uint8_t houseId = currentPlanetHouseId();
        drawImageAt(contractHouseNamesArchive_, static_cast<int>(houseId), 44, 37, false);
        drawImageAt(contractHouseEmblemsArchive_, static_cast<int>(houseId), 79, 91, false);
        drawImageAt(contractContactPortraitsArchive_, currentPlanetContractPortraitEntry(), 190, 37, false);
    }

    void renderStatusMenu() {
        drawImageFullScreenOrCentered(campaignArchive_, campaignBackdropEntry());
        drawGpPanel(25, 20, 186, 102, 8);
        drawGpPanel(158, 130, 142, 58, 8);
        drawStatusInfoPanel(25, 20, 186);
        drawStatusActionPanel(158, 130, 142);
    }

    void renderNewsNet() {
        drawNewsNetFrame();

        if (newsNetShowingNoOther_ || activeNewsNetMessageIndexes_.empty()) {
            drawNewsNetText5x5Centered(29, 99, 262, noOtherNewsNetMessageLine(), kNewsNetTextColor);
        } else {
            const int activeIndex = std::clamp(
                newsNetMessageIndex_,
                0,
                static_cast<int>(activeNewsNetMessageIndexes_.size()) - 1);
            const size_t messageIndex = activeNewsNetMessageIndexes_[static_cast<size_t>(activeIndex)];
            if (messageIndex < newsNetMessageLines_.size()) {
                drawNewsNetMessage(newsNetMessageLines_[messageIndex]);
            }
        }

        drawNewsNetButton(kNewsNetPreviousButtonRect, L"PREVIOUS", newsNetButtonIndex_ == 0);
        drawNewsNetButton(kNewsNetNextButtonRect, L"NEXT", newsNetButtonIndex_ == 1);
        drawNewsNetButton(kNewsNetDoneButtonRect, L"DONE", newsNetButtonIndex_ == 2, -3);
    }

    void renderContractAcceptedMessage() {
        drawNewsNetFrame();
        drawContractText7x5Centered(29, 99, 262, L"YOUR CONTRACT IS ACCEPTED.", kContractTextColor);
    }

    void drawNewsNetFrame() {
        fillRect(0, 0, kScreenWidth, kScreenHeight, 0);
        fillRect(29, 23, 262, 154, 0);
        drawImageAt(gpicsArchive_, 12, 0, 0, false);
        drawImageAt(gpicsArchive_, 45, 0, 23, false);
        drawImageAt(gpicsArchive_, 46, 291, 23, false);
        drawImageAt(gpicsArchive_, 44, 0, 177, false);
    }

    void renderContractMenu() {
        drawNewsNetFrame();

        const uint8_t houseId = currentPlanetHouseId();
        drawImageAt(contractHouseNamesArchive_, static_cast<int>(houseId), 44, 37, false);
        drawImageAt(contractHouseEmblemsArchive_, static_cast<int>(houseId), 79, 91, false);
        drawImageAt(contractContactPortraitsArchive_, currentPlanetContractPortraitEntry(), 190, 37, false);

        drawGpPanel(
            kContractMenuPanelRect.x,
            kContractMenuPanelRect.y,
            kContractMenuPanelRect.width,
            kContractMenuPanelRect.height,
            8);
        drawNewsNetButton(kContractMenuRequestButtonRect, L"REQUEST MISSION", contractMenuIndex_ == 0);
        drawNewsNetButton(kContractMenuLeaveButtonRect, L"LEAVE", contractMenuIndex_ == 1);
    }

    void renderContractNegotiation() {
        drawNewsNetFrame();
        if (contractNegotiationTerminated_) {
            drawContractText7x5(38, 39, L"YOUR OFFER IS UNACCEPTABLE!", kContractTextColor);
            drawContractText7x5(38, 47, L"ALL NEGOTIATIONS ARE HEREBY", kContractTextColor);
            drawContractText7x5(38, 55, L"TERMINATED.", kContractTextColor);
            return;
        }
        if (contractNegotiationUnavailable_) {
            drawContractText7x5(38, 39, L"NO CONTRACTS ARE AVAILABLE", kContractTextColor);
            drawContractText7x5(38, 47, L"AT THIS TIME.", kContractTextColor);
            return;
        }

        const ContractOffer* offer = activeContractOffer();
        if (!offer) {
            drawContractText7x5Centered(29, 94, 262, L"NO CONTRACTS ARE AVAILABLE", kContractTextColor);
            drawNewsNetButton(kNewsNetDoneButtonRect, L"LEAVE", true, -3);
            return;
        }

        drawContractText7x5Centered(29, 31, 262, L"MERCENARY CONTRACT", kContractTextColor);
        drawContractText7x5(45, 47, L"THIS AGREEMENT BETWEEN", kContractTextColor);
        drawContractText7x5(38, 55, L"HOUSE", kContractTextColor);
        drawContractText7x5(86, 55, houseNamePlain(offer->employerHouse), kContractVariableColor);
        drawContractText7x5(149, 55, L"AND BLAZING ACES", kContractTextColor);
        drawContractText7x5(38, 63, L"OUTLINES THE CONTRACT FOR", kContractTextColor);

        int y = 71;
        const std::vector<std::wstring> missionLines = wrapContractLine(offer->missionName, 31);
        for (const std::wstring& line : missionLines) {
            drawContractText7x5(38, y, line, kContractVariableColor);
            y += 8;
        }
        drawContractText7x5(38, y, L"ON THE", kContractTextColor);
        drawContractText7x5(94, y, houseNamePlain(offer->targetHouse), kContractVariableColor);
        drawContractText7x5(157, y, L"PLANET OF", kContractTextColor);
        y += 8;
        drawContractText7x5(38, y, offer->targetPlanet, kContractVariableColor);

        drawContractText7x5(38, 97, L"THE ESTIMATED ENEMY FORCE", kContractTextColor);
        drawContractText7x5(38, 105, L"IS", kContractTextColor);
        drawContractText7x5(62, 105, std::to_wstring(offer->heavyCount), kContractVariableColor);
        drawContractText7x5(86, 105, L"HEAVY,", kContractTextColor);
        drawContractText7x5(142, 105, std::to_wstring(offer->mediumCount), kContractVariableColor);
        drawContractText7x5(166, 105, L"MEDIUM AND", kContractTextColor);
        drawContractText7x5(38, 113, std::to_wstring(offer->lightCount), kContractVariableColor);
        drawContractText7x5(62, 113, L"LIGHT MECHS.", kContractTextColor);

        drawContractText7x5(38, 129, houseNamePlain(offer->employerHouse), kContractVariableColor);
        drawContractText7x5(102, 129, L"WILL PAY", kContractTextColor);
        drawContractValue7x5(kContractPriceValueRect.x, 129, std::to_wstring(offer->priceK), ContractEditableField::Price);
        drawContractText7x5(214, 129, L"K C-BILLS", kContractTextColor);
        drawContractText7x5(38, 137, L"AND", kContractTextColor);
        const std::wstring salvagePercentText = std::to_wstring(offer->salvagePercent);
        drawContractValue7x5(kContractSalvageValueRect.x, 137, salvagePercentText, ContractEditableField::Salvage);
        drawContractText7x5(
            std::max(102, kContractSalvageValueRect.x + newsNetButtonTextWidth(salvagePercentText) + 8),
            137,
            L"% OF ALL CONFISCATED",
            kContractTextColor);
        drawContractText7x5(38, 145, L"EQUIPMENT WITH", kContractTextColor);
        const std::wstring advancePercentText = std::to_wstring(offer->advancePercent);
        drawContractValue7x5(kContractAdvanceValueRect.x, 145, advancePercentText, ContractEditableField::Advance);
        drawContractText7x5(
            std::max(206, kContractAdvanceValueRect.x + newsNetButtonTextWidth(advancePercentText) + 8),
            145,
            L"% PAYABLE",
            kContractTextColor);
        drawContractText7x5(38, 153, L"IMMEDIATELY.", kContractTextColor);

        drawContractText7x5(38, 169, L"DATED", kContractTextColor);
        drawContractText7x5(86, 169, campaignDateLabel(), kContractVariableColor);

        drawNewsNetButton(
            kNewsNetPreviousButtonRect,
            offer->termsModified ? L"SUBMIT" : L"ACCEPT",
            contractNegotiationButtonIndex_ == 0);
        drawNewsNetButton(kNewsNetNextButtonRect, L"NEXT", contractNegotiationButtonIndex_ == 1);
        drawNewsNetButton(kNewsNetDoneButtonRect, L"LEAVE", contractNegotiationButtonIndex_ == 2, -3);
    }

    void renderMechLabMenu() {
        drawMechLabBackground();
        drawGpPanel(90, 124, 140, 64, 8);
        drawMenuTextCenteredInRect(90, 133, 140, L"MECH COMPLEX", 12);
        drawMechLabMenu(90, 147, 140);
    }

    void drawMechLabBackground() {
        drawImageFullScreenOrCentered(archive_, kMechLabBackgroundEntry);
        if (!mechLabWeldingActive_) {
            return;
        }

        const DWORD elapsed = GetTickCount() - mechLabWeldStartedTick_;
        int weldEntry = kMechLabWeldEndingEntry;
        if (elapsed + kMechLabWeldFrameMs < mechLabWeldDurationMs_) {
            const size_t frameIndex =
                static_cast<size_t>((elapsed / kMechLabWeldFrameMs) % kMechLabWeldEntries.size());
            weldEntry = kMechLabWeldEntries[frameIndex];
        }
        drawImageAt(gpicsArchive_, weldEntry, kMechLabWeldX, kMechLabWeldY, true);
    }

    void renderExtraAmmoMenu() {
        drawMechLabBackground();
        drawExtraAmmoCompatibilityPanel();
        drawGpPanel(
            kExtraAmmoMainPanelRect.x,
            kExtraAmmoMainPanelRect.y,
            kExtraAmmoMainPanelRect.width,
            kExtraAmmoMainPanelRect.height,
            8);

        drawMechStatusTextCentered(
            kExtraAmmoMainPanelRect.x,
            kExtraAmmoTitleY,
            kExtraAmmoMainPanelRect.width,
            L"BUY AMMO",
            12);
        drawMechStatusText(kExtraAmmoAmmoTypeX, kExtraAmmoHeaderY, L"AMMO TYPE", 12);
        drawMechStatusText(kExtraAmmoCostX, kExtraAmmoHeaderY, L"COST", 12);
        drawMechStatusText(kExtraAmmoHoldX, kExtraAmmoHeaderY, L"IN HOLD", 12);

        for (size_t i = 0; i < kAmmoDefinitions.size(); ++i) {
            const int y = kExtraAmmoFirstRowY + static_cast<int>(i) * kExtraAmmoLineStep;
            const uint8_t color = i == extraAmmoSelectionIndex_ ? 14 : 7;
            drawMechStatusText(kExtraAmmoAmmoTypeX, y, kAmmoDefinitions[i].label, color);
            drawMechStatusTextRightAligned(kExtraAmmoCostX, y, 38, formatWealth(extraAmmoCost(i)), 7);
            drawMechStatusTextRightAligned(
                kExtraAmmoHoldX,
                y,
                30,
                std::to_wstring(extraAmmoInHold_[i]),
                7);
        }

        drawMechStatusTextCentered(
            kExtraAmmoMainPanelRect.x,
            kExtraAmmoDoneY,
            kExtraAmmoMainPanelRect.width,
            L"DONE",
            extraAmmoSelectionIndex_ == kAmmoDefinitions.size() ? 14 : 7);
        drawMechStatusText(kExtraAmmoMainPanelRect.x + 62, kExtraAmmoWealthY, L"WEALTH:", 12);
        drawMechStatusText(kExtraAmmoMainPanelRect.x + 118, kExtraAmmoWealthY, formatWealth(playerWealth_), 7);
    }

    void drawExtraAmmoCompatibilityPanel() {
        drawGpPanel(
            kExtraAmmoCompatPanelRect.x,
            kExtraAmmoCompatPanelRect.y,
            kExtraAmmoCompatPanelRect.width,
            kExtraAmmoCompatPanelRect.height,
            8);

        const size_t ammoIndex = std::min(extraAmmoSelectionIndex_, kAmmoDefinitions.size() - 1u);
        const AmmoDefinition& ammo = kAmmoDefinitions[ammoIndex];
        drawMechStatusText(kExtraAmmoCompatPanelRect.x + 14, kExtraAmmoCompatPanelRect.y + 12, ammo.label, 7);
        int y = kExtraAmmoCompatPanelRect.y + 20;
        for (size_t i = 0; i < ammo.compatibleMechCount && i < ammo.compatibleMechs.size(); ++i) {
            drawMechStatusText(kExtraAmmoCompatPanelRect.x + 14, y, ammo.compatibleMechs[i], 7);
            y += 8;
        }
    }

    void renderMechReviewList() {
        drawImageFullScreenOrCentered(archive_, kMechLabBackgroundEntry);
        const RectI panel = mechReviewPanelRect();
        drawGpPanel(panel.x, panel.y, panel.width, panel.height, 8);
        drawMenuTextCenteredInRect(
            panel.x,
            kMechReviewTitleY,
            panel.width,
            L"MECHS OF THE BLAZING ACES",
            12);

        int y = kMechReviewItemY;
        for (size_t i = 0; i < ownedMechs_.size(); ++i) {
            const OwnedMech& mech = ownedMechs_[i];
            const uint8_t color = (i == selectedMechIndex_) ? 14 : 7;
            drawMenuTextCenteredInRect(kMechReviewPanelX + 12, y, 104, mech.name, color);
            drawMenuTextCenteredInRect(kMechReviewPanelX + 126, y, 86, pilotNameForCrewSlot(mech.assignedCrewSlot), color);
            y += kMechReviewLineStep;
        }

        const uint8_t doneColor = (selectedMechIndex_ == ownedMechs_.size()) ? 14 : 7;
        drawMenuTextCenteredInRect(panel.x, y, panel.width, L"DONE", doneColor);
    }

    void renderMechStatus() {
        const OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            changeState(ScreenState::MechReviewList);
            return;
        }

        drawMechStatusFrame();
        drawMechStatusTextCentered(0, kMechStatusTitleY, kMechStatusTextAreaWidth, L"MECH STATUS", 7);

        int y = kMechStatusFirstLineY;
        drawMechStatusInline(kMechStatusTextX, y, L"TYPE:", mech->name, 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"CONDITION:", mechConditionLabel(*mech), mechConditionColor(*mech));
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"REPAIR COST:", formatWealth(mech->repairCost), 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"WEIGHT:", std::to_wstring(mech->tons) + L" TONS", 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"SPEED:", std::to_wstring(mech->speedKph) + L" KPH", 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"JUMP CAP:", std::to_wstring(mech->jumpCapMeters) + L" M", 12);
        y += kMechStatusLineStep;
        drawMechStatusText(kMechStatusTextX, y, L"AMMO:", 7);
        drawMechAmmoLines(*mech, y + kMechStatusLineStep);

        drawMechStatusMenu();
        drawMechStatusImage(*mech);
    }

    void renderMechRepairStatus() {
        const OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            changeState(ScreenState::MechReviewList);
            return;
        }

        drawMechStatusFrame();
        drawMechStatusTextCentered(0, kMechRepairTitleY, kMechStatusTextAreaWidth, mech->name, 12);

        int y = kMechRepairFirstLineY;
        drawRepairComponentLine(*mech, y, L"ENGINE:", RepairTargetKind::Engine, mech->engine);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"GYROS:", RepairTargetKind::Gyros, mech->gyros);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"SENSORS:", RepairTargetKind::Sensors, mech->sensors);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"LIFE SUPPORT:", RepairTargetKind::LifeSupport, mech->lifeSupport);
        y += kMechRepairComponentLineStep;
        drawRepairCountLine(*mech, y, L"HEAT SINK   :", RepairTargetKind::HeatSink, mech->heatSinksWorking, mech->heatSinksTotal);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"LA ACTUATOR:", RepairTargetKind::LeftArmActuator, mech->leftArmActuator);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"RA ACTUATOR:", RepairTargetKind::RightArmActuator, mech->rightArmActuator);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"LL ACTUATOR:", RepairTargetKind::LeftLegActuator, mech->leftLegActuator);
        y += kMechRepairComponentLineStep;
        drawRepairComponentLine(*mech, y, L"RL ACTUATOR:", RepairTargetKind::RightLegActuator, mech->rightLegActuator);
        y += kMechRepairComponentLineStep;
        drawRepairCountLine(*mech, y, L"JUMP JETS:", RepairTargetKind::JumpJets, mech->jumpJetsWorking, mech->jumpJetsTotal);
        y += kMechRepairComponentLineStep;
        drawRepairArmorLine(*mech, y);

        drawRepairWeaponTable(*mech);
        drawMechStatusTextCentered(0, kMechRepairDoneY, kMechStatusTextAreaWidth, L"DONE", 7);
        drawRepairCostLine(selectedRepairCost(*mech));
        drawMechStatusText(12, kMechRepairWealthY, L"WEALTH:" + formatWealth(playerWealth_), 7);

        drawMechStatusImage(*mech);
    }

    void renderMechReloadPrompt() {
        const OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            changeState(ScreenState::MechReviewList);
            return;
        }

        renderMechStatus();
        drawGpPanel(
            kMechReloadPanelRect.x,
            kMechReloadPanelRect.y,
            kMechReloadPanelRect.width,
            kMechReloadPanelRect.height,
            8);

        drawMechStatusText(26, 153, L"RELOAD COST:", 7);
        drawMechStatusText(98, 153, formatWealth(reloadCost(*mech)), 7);
        drawMechStatusText(26, 165, L"WEALTH:", 7);
        drawMechStatusText(78, 165, formatWealth(playerWealth_), 7);

        drawMechStatusTextCentered(
            kMechReloadReloadButtonRect.x,
            kMechReloadReloadButtonRect.y,
            kMechReloadReloadButtonRect.width,
            L"RELOAD",
            reloadSelectionIndex_ == 0 ? 14 : 7);
        drawMechStatusTextCentered(
            kMechReloadCancelButtonRect.x,
            kMechReloadCancelButtonRect.y,
            kMechReloadCancelButtonRect.width,
            L"CANCEL",
            reloadSelectionIndex_ == 1 ? 14 : 7);
    }

    void renderMechSellOffer() {
        const OwnedMech* mech = selectedOwnedMech();
        if (!mech) {
            changeState(ScreenState::MechLabMenu);
            return;
        }

        renderMechStatus();
        drawGpPanel(
            kMechSellOfferPanelRect.x,
            kMechSellOfferPanelRect.y,
            kMechSellOfferPanelRect.width,
            kMechSellOfferPanelRect.height,
            8);

        drawRecruitText7x5(kMechSellOfferPanelRect.x + 16, kMechSellOfferPanelRect.y + 12, L"I AM PREPARED TO", 7, 160);
        drawRecruitText7x5(kMechSellOfferPanelRect.x + 16, kMechSellOfferPanelRect.y + 20, L"OFFER YOU THE SUM OF", 7, 160);
        drawRecruitTextCenteredInRect(
            kMechSellOfferPanelRect.x,
            kMechSellOfferPanelRect.y + 28,
            kMechSellOfferPanelRect.width,
            formatWealth(mechSellOffer(*mech)) + L" C-BILLS",
            7);
        drawRecruitText7x5(kMechSellOfferPanelRect.x + 16, kMechSellOfferPanelRect.y + 36, L"FOR YOUR MECH.", 7, 160);

        drawRecruitTextCenteredInRect(
            kMechSellAcceptButtonRect.x,
            kMechSellAcceptButtonRect.y,
            kMechSellAcceptButtonRect.width,
            L"ACCEPT",
            sellOfferSelectionIndex_ == 0 ? 14 : 7);
        drawRecruitTextCenteredInRect(
            kMechSellRejectButtonRect.x,
            kMechSellRejectButtonRect.y,
            kMechSellRejectButtonRect.width,
            L"REJECT",
            sellOfferSelectionIndex_ == 1 ? 14 : 7);
    }

    void renderMechBuyList() {
        ensureCurrentPlanetMechMarket();
        const PlanetMechMarket& market = currentPlanetMechMarket();
        const RectI panel = mechBuyListPanelRect();

        if (market.mechsForSale.empty()) {
            renderMechLabMenu();
            drawGpPanel(
                panel.x,
                panel.y,
                panel.width,
                panel.height,
                8);
            drawRecruitText7x5(panel.x + 16, panel.y + 12, L"SORRY, NO MECHS FOR SALE!", 7, panel.width - 24);
            drawRecruitText7x5(panel.x + 16, panel.y + 22, L"TRY BACK NEXT WEEK.", 7, panel.width - 24);
            return;
        }

        drawMechLabBackground();
        drawGpPanel(
            panel.x,
            panel.y,
            panel.width,
            panel.height,
            8);

        drawMenuTextCenteredInRect(
            panel.x,
            kMechBuyListTitleY,
            panel.width,
            L"MECHS FOR SALE",
            12);

        const size_t visible = visibleMarketRows(market.mechsForSale.size());
        for (size_t row = 0; row < visible; ++row) {
            const size_t index = mechMarketScrollOffset_ + row;
            const int y = kMechBuyListFirstRowY + static_cast<int>(row) * kMechBuyListLineStep;
            const uint8_t color = index == selectedMarketMechIndex_ ? 14 : 7;
            drawMenuTextCenteredInRect(
                panel.x,
                y,
                panel.width,
                market.mechsForSale[index].mech.name,
                color);
        }

        const uint8_t doneColor = selectedMarketMechIndex_ == market.mechsForSale.size() ? 14 : 7;
        drawMenuTextCenteredInRect(
            panel.x,
            mechBuyListDoneY(market.mechsForSale.size()),
            panel.width,
            L"DONE",
            doneColor);
    }

    void renderMechBuyStatus() {
        const MarketMech* offer = selectedMarketMech();
        if (!offer) {
            changeState(ScreenState::MechBuyList);
            return;
        }

        const OwnedMech& mech = offer->mech;
        drawMechStatusFrame();
        drawMechStatusTextCentered(0, kMechStatusTitleY, kMechStatusTextAreaWidth, L"MECH STATUS", 7);

        int y = kMechStatusFirstLineY;
        drawMechStatusInline(kMechStatusTextX, y, L"TYPE:", mech.name, 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"CONDITION:", mechConditionLabel(mech), mechConditionColor(mech));
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"REPAIR COST:", formatWealth(mech.repairCost), 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"WEIGHT:", std::to_wstring(mech.tons) + L" TONS", 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"SPEED:", std::to_wstring(mech.speedKph) + L" KPH", 12);
        y += kMechStatusLineStep;
        drawMechStatusInline(kMechStatusTextX, y, L"JUMP CAP:", std::to_wstring(mech.jumpCapMeters) + L" M", 12);
        y += kMechStatusLineStep;
        drawMechStatusText(kMechStatusTextX, y, L"AMMO:", 7);
        drawMechAmmoLines(mech, y + kMechStatusLineStep);

        drawMechStatusInline(kMechStatusTextX, 134, L"PRICE:", formatWealth(offer->askingPrice), 12);
        drawMechStatusInline(kMechStatusTextX, 146, L"WEALTH:", formatWealth(playerWealth_), 12);
        drawMechBuyStatusMenu();
        drawMechStatusImage(mech);
    }

    void renderMechBuyDamageStatus() {
        const MarketMech* offer = selectedMarketMech();
        if (!offer) {
            changeState(ScreenState::MechBuyList);
            return;
        }

        const OwnedMech& mech = offer->mech;
        drawMechStatusFrame();
        drawMechStatusTextCentered(0, kMechRepairTitleY, kMechStatusTextAreaWidth, mech.name, 12);

        int y = kMechRepairFirstLineY;
        drawDamageComponentLine(y, L"ENGINE:", mech.engine);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"GYROS:", mech.gyros);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"SENSORS:", mech.sensors);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"LIFE SUPPORT:", mech.lifeSupport);
        y += kMechRepairComponentLineStep;
        drawDamageCountLine(y, L"HEAT SINK   :", mech.heatSinksWorking, mech.heatSinksTotal);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"LA ACTUATOR:", mech.leftArmActuator);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"RA ACTUATOR:", mech.rightArmActuator);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"LL ACTUATOR:", mech.leftLegActuator);
        y += kMechRepairComponentLineStep;
        drawDamageComponentLine(y, L"RL ACTUATOR:", mech.rightLegActuator);
        y += kMechRepairComponentLineStep;
        drawDamageCountLine(y, L"JUMP JETS:", mech.jumpJetsWorking, mech.jumpJetsTotal);
        y += kMechRepairComponentLineStep;
        drawDamageArmorLine(mech, y);

        drawDamageWeaponTable(mech);
        drawMechStatusTextCentered(0, kMechRepairDoneY, kMechStatusTextAreaWidth, L"DONE", 14);
        drawMechStatusImage(mech);
    }

    void renderMechBuyMessage(std::wstring_view line1, std::wstring_view line2 = {}) {
        drawGpPanel(
            kMechBuyMessagePanelRect.x,
            kMechBuyMessagePanelRect.y,
            kMechBuyMessagePanelRect.width,
            kMechBuyMessagePanelRect.height,
            8);

        if (line2.empty()) {
            drawRecruitTextCenteredInRect(
                kMechBuyMessagePanelRect.x,
                kMechBuyMessagePanelRect.y + 16,
                kMechBuyMessagePanelRect.width,
                line1,
                7);
            return;
        }

        drawRecruitTextCenteredInRect(
            kMechBuyMessagePanelRect.x,
            kMechBuyMessagePanelRect.y + 11,
            kMechBuyMessagePanelRect.width,
            line1,
            7);
        drawRecruitTextCenteredInRect(
            kMechBuyMessagePanelRect.x,
            kMechBuyMessagePanelRect.y + 21,
            kMechBuyMessagePanelRect.width,
            line2,
            7);
    }

    void renderBarMenu() {
        drawImageFullScreenOrCentered(barArchive_, kBarBaseEntry);
        const int overlayEntry = barEnvironmentOverlayEntry();
        if (overlayEntry >= 0) {
            drawImageAt(barEnvironmentOverlayArchive_, overlayEntry, 0, 0, false);
        }
        if (barDialogState_ == BarDialogState::None) {
            drawGpPanel(166, 130, 128, 58, 8);
            drawMenuTextCenteredInRect(166, 135, 128, L"BAR", 12);
            drawBarMenu(166, 150, 128);
        }
        renderBarDialog();
    }

    void renderBarDialog() {
        if (barDialogState_ == BarDialogState::None) {
            return;
        }

        if (barDialogState_ == BarDialogState::RecruitOffer) {
            renderRecruitOfferDialog();
        } else if (barDialogState_ == BarDialogState::NoCandidates) {
            drawGpPanel(
                kRecruitNoCandidatesPanelRect.x,
                kRecruitNoCandidatesPanelRect.y,
                kRecruitNoCandidatesPanelRect.width,
                kRecruitNoCandidatesPanelRect.height,
                8);
            drawRecruitTextCenteredInRect(
                kRecruitNoCandidatesPanelRect.x,
                kRecruitNoCandidatesPanelRect.y + 15,
                kRecruitNoCandidatesPanelRect.width,
                L"THERE'S NOBODY AROUND RIGHT NOW.",
                7);
        } else if (barDialogState_ == BarDialogState::CrewFull) {
            drawGpPanel(
                kRecruitCrewFullPanelRect.x,
                kRecruitCrewFullPanelRect.y,
                kRecruitCrewFullPanelRect.width,
                kRecruitCrewFullPanelRect.height,
                8);
            drawRecruitText7x5(kRecruitCrewFullPanelRect.x + 16, kRecruitCrewFullPanelRect.y + 9, L"SORRY!", 7, 120);
            drawRecruitText7x5(kRecruitCrewFullPanelRect.x + 16, kRecruitCrewFullPanelRect.y + 17, L"YOU CAN'T HIRE", 7, 120);
            drawRecruitText7x5(kRecruitCrewFullPanelRect.x + 16, kRecruitCrewFullPanelRect.y + 25, L"MORE THAN THREE", 7, 120);
            drawRecruitText7x5(kRecruitCrewFullPanelRect.x + 16, kRecruitCrewFullPanelRect.y + 33, L"CREW MEMBERS", 7, 120);
        }
    }

    void renderRecruitOfferDialog() {
        if (activeRecruitIndex_ < 0 || static_cast<size_t>(activeRecruitIndex_) >= recruitPilots_.size()) {
            return;
        }

        const RecruitPilot& pilot = recruitPilots_[static_cast<size_t>(activeRecruitIndex_)];
        drawGpPanel(
            kRecruitMainPanelRect.x,
            kRecruitMainPanelRect.y,
            kRecruitMainPanelRect.width,
            kRecruitMainPanelRect.height,
            8);
        drawGpPanel(
            kRecruitStatsPanelRect.x,
            kRecruitStatsPanelRect.y,
            kRecruitStatsPanelRect.width,
            kRecruitStatsPanelRect.height,
            8);

        drawImageAt(crewArchive_, pilot.portraitEntry, 210, 58, false);

        constexpr uint8_t kOfferTextColor = 12;
        drawRecruitText7x5(26, 20, L"HEARD YOU MIGHT", kOfferTextColor, 160);
        drawRecruitText7x5(26, 28, L"BE LOOKING FOR", kOfferTextColor, 160);
        drawRecruitText7x5(26, 36, L"A GOOD MECH PILOT.", kOfferTextColor, 160);

        int quoteY = 52;
        for (const std::wstring& line : pilot.quoteLines) {
            drawRecruitText7x5(26, quoteY, line, kOfferTextColor, 160);
            quoteY += 8;
        }

        drawRecruitText7x5(26, 92, L"YOU INTERESTED?", kOfferTextColor, 160);
        drawRecruitText7x5(94, 102, L"YES", recruitChoiceIndex_ == 0 ? 14 : 7, 50);
        drawRecruitText7x5(100, 112, L"NO", recruitChoiceIndex_ == 1 ? 14 : 7, 50);

        drawRecruitText7x5(160, 146, L"NAME", 7, 45);
        drawRecruitText7x5(160, 154, L"GUNRY", 7, 45);
        drawRecruitText7x5(160, 162, L"PILOT", 7, 45);
        drawRecruitText7x5(160, 170, L"PAY", 7, 45);
        drawRecruitText7x5(210, 146, pilot.name, 7, 75);
        drawRecruitText7x5(210, 154, skillLabel(pilot.gunnerySkill), 7, 75);
        drawRecruitText7x5(210, 162, skillLabel(pilot.pilotingSkill), 7, 75);
        drawRecruitText7x5(210, 170, std::to_wstring(pilot.monthlyWage), 7, 75);
    }

    void renderSystemMenu() {
        drawImageFullScreenOrCentered(campaignArchive_, campaignBackdropEntry());
        drawGpPanel(90, 80, 140, 86, 8);
        drawSystemMenu(90, 90, 140);
    }

    void renderSaveGameNameInput() {
        drawImageFullScreenOrCentered(campaignArchive_, campaignBackdropEntry());
        constexpr RectI panel{40, 102, 240, 55};
        drawGpPanel(panel.x, panel.y, panel.width, panel.height, 8);
        drawRecruitTextCenteredInRect(
            panel.x,
            panel.y + 17,
            panel.width,
            L"ENTER A NAME FOR THIS GAME",
            7);

        const std::wstring input = widen(saveGameNameInput_);
        const int inputWidth = recruitTextWidth7x5(input);
        const int inputX = panel.x + (panel.width - inputWidth) / 2;
        const int inputY = panel.y + 37;
        if (!input.empty()) {
            drawRecruitText7x5(inputX, inputY, input, 4, panel.width - 20);
        }
        if (((GetTickCount() - stateStartedTick_) / 350u) % 2u == 0u) {
            const int cursorX = input.empty() ? panel.x + panel.width / 2 - 3 : inputX + inputWidth + 4;
            drawRecruitText7x5(cursorX, inputY, L"*", 4, 10);
        }
    }

    void renderRestoreGameList() {
        drawImageFullScreenOrCentered(campaignArchive_, campaignBackdropEntry());
        constexpr RectI panel{52, 22, 216, 160};
        constexpr int firstSlotY = 47;
        constexpr int lineStep = 10;
        drawGpPanel(panel.x, panel.y, panel.width, panel.height, 8);
        drawRecruitTextCenteredInRect(
            panel.x,
            panel.y + 12,
            panel.width,
            L"SELECT A GAME TO LOAD",
            12);

        for (size_t i = 0; i < kGamVisibleSlotCount; ++i) {
            std::wstring line = std::to_wstring(i + 1u) + L":";
            if (i < restoreGameSlots_.size() && restoreGameSlots_[i].occupied) {
                line += L"  ";
                line += restoreGameSlots_[i].name;
            }
            drawRecruitText7x5(
                panel.x + 68,
                firstSlotY + static_cast<int>(i) * lineStep,
                line,
                i == restoreGameSelectionIndex_ ? 14 : 7,
                panel.width - 80);
        }

        drawRecruitTextCenteredInRect(
            panel.x,
            panel.y + panel.height - 15,
            panel.width,
            L"CANCEL",
            restoreGameSelectionIndex_ == kRestoreGameCancelIndex ? 14 : 7);
    }

    void renderCrewMenu() {
        constexpr int kDecorationCellX = 0;
        constexpr uint8_t kBackgroundColor = 8;
        constexpr uint8_t kLineColor = 7;
        constexpr uint8_t kTextColor = 7;
        constexpr uint8_t kSelectedTextColor = 14;
        constexpr uint8_t kAssignmentTextColor = 10;

        fillRect(0, 0, kScreenWidth, kScreenHeight, kBackgroundColor);

        drawImageAt(crewArchive_, kCrewDecorationEntry, kDecorationCellX, kCrewTopRowY, false);
        drawImageAt(crewArchive_, kCrewDecorationEntry, kDecorationCellX, kCrewMechImageRowY, false);

        for (size_t i = 0; i < crewMembers_.size(); ++i) {
            const CrewMember& member = crewMembers_[i];
            if (!member.hired) {
                continue;
            }
            if (member.portraitEntry >= 0) {
                drawImageAt(crewArchive_, member.portraitEntry, kCrewPilotCellXs[i], kCrewTopRowY, false);
            }
            const int mechImageEntry = crewMechImageEntry(i);
            if (mechImageEntry >= 0) {
                drawImageAt(crewMechArchive_, mechImageEntry, kCrewPilotCellXs[i], kCrewMechImageRowY, false);
            }
        }

        drawCrewGridLines(kLineColor);

        drawCrewText5x5(kCrewLabelX, 71, L"NAME", kTextColor, 40);
        drawCrewText5x5(kCrewLabelX, 79, L"GUNRY", kTextColor, 40);
        drawCrewText5x5(kCrewLabelX, 87, L"PILOT", kTextColor, 40);
        drawCrewText5x5(kCrewLabelX, 103, L"WAGE", kTextColor, 40);
        drawCrewText5x5(kCrewLabelX, 111, L"MECH", kTextColor, 40);
        drawCrewText5x5(
            kCrewLabelX,
            122,
            L"DONE",
            crewInteractionMode_ == CrewInteractionMode::Navigate &&
                    crewSelectionIndex_ == kCrewDoneSelectionIndex
                ? kSelectedTextColor
                : kTextColor,
            40);

        for (size_t i = 0; i < crewMembers_.size(); ++i) {
            const CrewMember& member = crewMembers_[i];
            if (!member.hired) {
                continue;
            }
            const int x = (i == 0) ? kCrewValueX : kCrewPilotCellXs[i] + 4;
            drawCrewText5x5(x, 71, member.name, kTextColor, kCrewCellWidth - 6);
            drawCrewText5x5(x, 79, member.gunnery, kTextColor, kCrewCellWidth - 6);
            drawCrewText5x5(x, 87, member.piloting, kTextColor, kCrewCellWidth - 6);
            if (member.wage > 0) {
                const std::wstring wageLabel = std::to_wstring(member.wage);
                drawCrewText5x5(x, 103, wageLabel, kTextColor, kCrewCellWidth - 6);
            }

            std::wstring assignmentLabel;
            std::wstring_view mechLabel = crewMechLabel(i);
            uint8_t mechTextColor = kTextColor;
            if (crewSelectionIndex_ == i) {
                if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                    assignmentLabel = crewAssignmentOptionLabel(i, crewAssignmentIndex_);
                    mechLabel = assignmentLabel;
                    mechTextColor = kAssignmentTextColor;
                } else {
                    mechTextColor = kSelectedTextColor;
                }
            }
            drawCrewText5x5(x, 111, mechLabel, mechTextColor, kCrewCellWidth - 6);
        }
    }

    void renderStarmap() {
        drawImageFullScreenOrCentered(archive_, kStarmapBackgroundEntry);
        if (planets_.empty()) {
            drawSmallTextPlain(8, 8, L"PLANET DATA NOT LOADED", 15);
            return;
        }

        const PlanetRecord& currentPlanet =
            planets_[std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1)];
        const PlanetRecord& selectedPlanet =
            planets_[std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1)];

        drawHouseEmblem(selectedPlanet.houseId);
        if (state_ == ScreenState::TravelRoutePreview) {
            drawStarmapRouteLine(currentPlanet, selectedPlanet);
        }
        drawStarmapMarker(currentPlanet.mapX, currentPlanet.mapY, 15);
        drawStarmapMarker(selectedPlanet.mapX, selectedPlanet.mapY, 15);

        if (starmapNameInputActive_) {
            drawStarmapText5x5(10, 8, widen(starmapNameInput_), 12, 112);
            drawStarmapNameInputCursor(10 + static_cast<int>(starmapNameInput_.size()) * 6, 7);
        } else {
            drawStarmapText5x5(10, 8, widen(selectedPlanet.name), 15, 112);
        }
        drawStarmapText5x5Centered(135, 8, 60, environmentLabel(selectedPlanet.terrainCode), 15);
        drawStarmapText5x5Centered(204, 8, 108, L"POP:" + formatWealth(selectedPlanet.population), 15);

        drawStarmapText5x5(10, 23, widen(selectedPlanet.description), 15, 300);

        const uint16_t jumpCount = travelJumpCount(currentPlanet, selectedPlanet);
        const uint64_t travelCost = travelCostForJumps(jumpCount);
        drawStarmapText5x5(240, 105, formatWealth(travelCost), 15, 70);
        drawStarmapText5x5(240, 129, formatWealth(playerWealth_), 15, 70);

        drawStarmapText5x5Centered(249, 147, 52, L"TRAVEL", starmapButtonIndex_ == 0 ? 14 : 15);
        drawStarmapText5x5Centered(249, 166, 52, L"PLANETS", starmapButtonIndex_ == 1 ? 14 : 15);
        drawStarmapText5x5Centered(249, 185, 52, L"CANCEL", starmapButtonIndex_ == 2 ? 14 : 15);

        if (starmapMenuMode_ == StarmapMenuMode::Houses) {
            renderStarmapHouseMenu();
        } else if (starmapMenuMode_ == StarmapMenuMode::HousePlanets) {
            renderStarmapPlanetMenu();
        }
    }

    void renderStarmapHouseMenu() {
        const RectI rect = starmapHouseMenuRect();
        drawGpPanel(rect.x, rect.y, rect.width, rect.height, 0);
        constexpr int kTextX = 140;
        constexpr int kTitleY = 80;
        constexpr int kFirstItemY = 90;
        constexpr int kLineStep = 8;
        drawStarmapText5x5(kTextX, kTitleY, L"HOUSES", 12, rect.x + rect.width - kTextX - 6);

        for (size_t i = 0; i < kHouseMenuItemCount; ++i) {
            const uint8_t color = starmapHouseSelectionIndex_ == i ? 14 : 7;
            drawStarmapText5x5(
                kTextX,
                kFirstItemY + static_cast<int>(i) * kLineStep,
                houseNamePlain(static_cast<uint8_t>(i)),
                color,
                rect.x + rect.width - kTextX - 6);
        }

        drawStarmapText5x5(
            kTextX,
            kFirstItemY + static_cast<int>(kHouseMenuItemCount) * kLineStep,
            L"DONE",
            starmapHouseSelectionIndex_ == kHouseMenuItemCount ? 14 : 7,
            rect.x + rect.width - kTextX - 6);
    }

    void renderStarmapPlanetMenu() {
        const RectI rect = starmapPlanetMenuRect();
        drawGpPanel(rect.x, rect.y, rect.width, rect.height, 0);

        const std::wstring title =
            L"HOUSE " +
            std::wstring(houseNamePlain(static_cast<uint8_t>(std::min<size_t>(starmapHouseSelectionIndex_, kHouseMenuItemCount - 1u))));
        drawStarmapText5x5Centered(rect.x, 39, rect.width, title, 12);

        const std::vector<int> indexes = starmapPlanetIndexesForSelectedHouse();
        constexpr int kRowsPerColumn = 16;
        constexpr int kLeftX = 65;
        constexpr int kRightX = 172;
        constexpr int kFirstY = 55;
        constexpr int kLineStep = 8;
        for (size_t i = 0; i < indexes.size(); ++i) {
            const int column = static_cast<int>(i / kRowsPerColumn);
            if (column > 1) {
                break;
            }
            const int row = static_cast<int>(i % kRowsPerColumn);
            const int x = column == 0 ? kLeftX : kRightX;
            const int y = kFirstY + row * kLineStep;
            const PlanetRecord& planet = planets_[static_cast<size_t>(indexes[i])];
            drawStarmapText5x5(x, y, widen(planet.name), starmapPlanetSelectionIndex_ == i ? 14 : 7, 96);
        }

        drawStarmapText5x5Centered(
            rect.x,
            185,
            rect.width,
            L"CANCEL",
            starmapPlanetSelectionIndex_ >= indexes.size() ? 14 : 7);
    }

    void renderTravelAnimation() {
        drawImageFullScreenOrCentered(travelShuttleArchive_, kTravelShuttleEntry);
        const DWORD elapsed = GetTickCount() - stateStartedTick_;
        if (elapsed < kTravelEngineDelayMs || travelEngineArchive_.images.empty()) {
            return;
        }

        const DWORD engineElapsed = elapsed - kTravelEngineDelayMs;
        const size_t frameIndex = std::min<size_t>(
            static_cast<size_t>(engineElapsed / kTravelEngineFrameMs),
            kTravelEngineSequence.size() - 1u);
        drawImageAt(
            travelEngineArchive_,
            kTravelEngineSequence[frameIndex],
            kTravelEngineX,
            kTravelEngineY,
            false);
    }

    void renderBattleStub() {
        fillRect(0, 0, kScreenWidth, kScreenHeight, 0);
        drawGpPanel(70, 50, 180, 108, 8);
        if (finalMissionStubActive_) {
            drawRecruitTextCenteredInRect(70, 58, 180, L"FINAL MISSION STUB", 7);
            drawRecruitTextCenteredInRect(70, 66, 180, L"DARK WING BASE", 7);
        }
        drawNewsNetButton(kBattleStubWinButtonRect, L"WIN BATTLE", battleStubButtonIndex_ == 0);
        drawNewsNetButton(kBattleStubLoseButtonRect, L"LOSE BATTLE", battleStubButtonIndex_ == 1);
        drawNewsNetButton(kBattleStubRunButtonRect, L"DIE IN BATTLE", battleStubButtonIndex_ == 2);
    }

    void renderMissionDebrief() {
        fillRect(0, 0, kScreenWidth, kScreenHeight, 7);
        drawDebriefPanel(
            kMissionDebriefTopPanelRect.x,
            kMissionDebriefTopPanelRect.y,
            kMissionDebriefTopPanelRect.width,
            kMissionDebriefTopPanelRect.height);
        fillRect(156, 10, 1, 122, 0);

        drawImageAt(missionResultArchive_, missionDebriefImageEntry(), 9, 7, false);

        drawDebriefText(162, 16, L"CREW STATUS:", 0);
        int crewY = 28;
        for (const MissionParticipant& participant : missionParticipants_) {
            if (crewY > 56) {
                break;
            }
            drawDebriefText(162, crewY, participant.name, 0);
            drawDebriefText(250, crewY, participant.killed ? L"KILLED" : L"OK", 0);
            crewY += 10;
        }

        drawDebriefText(162, 76, L"MECH STATUS", 0);
        int mechY = 88;
        for (const MissionParticipant& participant : missionParticipants_) {
            if (mechY > 108) {
                break;
            }
            drawDebriefText(162, mechY, participant.mechName, 0);
            drawDebriefTextRightAligned(294, mechY, std::to_wstring(participant.armorPercent) + L" %", 0);
            mechY += 10;
        }

        drawDebriefText(162, 118, L"SALVAGE:", 0);
        drawDebriefText(234, 118, formatWealth(missionDebriefSalvage_), 0);
        drawDebriefText(162, 129, L"PAYMENT:", 0);
        drawDebriefText(234, 129, formatWealth(missionDebriefPayment_), 0);

        drawDebriefPanel(
            kMissionDebriefBottomPanelRect.x,
            kMissionDebriefBottomPanelRect.y,
            kMissionDebriefBottomPanelRect.width,
            kMissionDebriefBottomPanelRect.height);
        const std::vector<std::wstring>& messageLines = missionDebriefMessageLines();
        int messageY = 158;
        for (const std::wstring& line : messageLines) {
            if (line.empty()) {
                continue;
            }
            if (messageY > 184) {
                break;
            }
            drawNewsNetText5x5(10, messageY, line, 0, kMissionDebriefMessageTextWidth);
            messageY += 8;
        }
        drawImageAt(
            houseEmblemsArchive_,
            static_cast<int>(acceptedContract_.employerHouse),
            kMissionDebriefEmblemX,
            kMissionDebriefEmblemY,
            false);

        if (missionDebriefOutcome_ == MissionOutcome::Death) {
            renderMissionDeathMenu();
        }
    }

    int missionDebriefImageEntry() const {
        if (missionDebriefOutcome_ == MissionOutcome::Victory) {
            return kMissionResultVictoryImageEntry;
        }
        if (missionDebriefOutcome_ == MissionOutcome::Death) {
            return kMissionResultDeathImageEntry;
        }
        return kMissionResultDefeatImageEntry;
    }

    const std::vector<std::wstring>& missionDebriefMessageLines() const {
        if (missionDebriefOutcome_ == MissionOutcome::Victory) {
            return missionVictoryLines_;
        }
        if (missionDebriefOutcome_ == MissionOutcome::Death) {
            static const std::vector<std::wstring> kNoDeathHouseLines;
            return kNoDeathHouseLines;
        }
        return missionDefeatLines_;
    }

    void renderMissionDeathMenu() {
        drawGpPanel(25, 72, 270, 54, 8);
        if (missionDeathPromptLines_.size() >= 2) {
            drawNewsNetText5x5(37, 83, missionDeathPromptLines_[0], 7, 250);
            drawNewsNetText5x5(37, 91, missionDeathPromptLines_[1], 7, 250);
        } else {
            drawNewsNetText5x5(37, 83, L"SUDDEN DEATH IS A GRIM REALITY IN THE", 7, 250);
            drawNewsNetText5x5(37, 91, L"SUCCESSOR STATES OF THE 31ST CENTURY.", 7, 250);
        }
        drawNewsNetText5x5(48, 106, L"PLAY AGAIN", missionDeathMenuIndex_ == 0 ? 14 : 7, 100);
        drawNewsNetText5x5(48, 113, L"QUIT", missionDeathMenuIndex_ == 1 ? 14 : 7, 60);
    }

    void drawDebriefText(int x, int y, std::wstring_view text, uint8_t color) {
        const Font& debriefFont = menuFont_.rows.empty() ? font_ : menuFont_;
        int cursorX = x;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX > 309) {
                break;
            }
            drawGlyph7x5(debriefFont, cursorX, y, ch, color);
            cursorX += 8;
        }
    }

    void drawDebriefTextRightAligned(int rightX, int y, std::wstring_view text, uint8_t color) {
        drawDebriefText(rightX - newsNetButtonTextWidth(text), y, text, color);
    }


    void drawImageFullScreenOrCentered(int entryIndex) {
        drawImageFullScreenOrCentered(archive_, entryIndex);
    }

    void drawImageFullScreenOrCentered(const PicsArchive& archive, int entryIndex) {
        const auto it = std::find_if(
            archive.images.begin(),
            archive.images.end(),
            [entryIndex](const Image4bpp& image) { return image.entryIndex == entryIndex; });
        if (it != archive.images.end()) {
            drawImageCentered(*it, archive);
        }
    }

    void drawImageAt(const PicsArchive& archive, int entryIndex, int dstX, int dstY, bool transparentZero) {
        const auto it = std::find_if(
            archive.images.begin(),
            archive.images.end(),
            [entryIndex](const Image4bpp& image) { return image.entryIndex == entryIndex; });
        if (it != archive.images.end()) {
            drawImageAt(*it, archive, dstX, dstY, transparentZero);
        }
    }

    void drawImageAtClipped(
        const PicsArchive& archive,
        int entryIndex,
        int dstX,
        int dstY,
        bool transparentZero,
        const RectI& clip) {
        const auto it = std::find_if(
            archive.images.begin(),
            archive.images.end(),
            [entryIndex](const Image4bpp& image) { return image.entryIndex == entryIndex; });
        if (it != archive.images.end()) {
            drawImageAt(*it, archive, dstX, dstY, transparentZero, &clip);
        }
    }

    void fillRect(int x, int y, int width, int height, uint8_t color) {
        const uint32_t bgra = toBgra(paletteColor(color));
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(kScreenWidth, x + width);
        const int y1 = std::min(kScreenHeight, y + height);
        for (int dstY = y0; dstY < y1; ++dstY) {
            auto row = framebuffer_.begin() + static_cast<size_t>(dstY) * kScreenWidth;
            std::fill(row + x0, row + x1, bgra);
        }
    }

    void drawPixel(int x, int y, uint8_t color) {
        if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) {
            return;
        }
        framebuffer_[static_cast<size_t>(y) * kScreenWidth + x] = toBgra(paletteColor(color));
    }

    void drawBox(int x, int y, int width, int height, uint8_t color) {
        for (int dx = 0; dx < width; ++dx) {
            drawPixel(x + dx, y, color);
            drawPixel(x + dx, y + height - 1, color);
        }
        for (int dy = 0; dy < height; ++dy) {
            drawPixel(x, y + dy, color);
            drawPixel(x + width - 1, y + dy, color);
        }
    }

    void drawGpPanel(int x, int y, int width, int height, uint8_t fillColor) {
        fillRect(x + 6, y + 4, width - 12, height - 8, fillColor);
        drawGpFrame(x, y, width, height);
    }

    void drawDebriefPanel(int x, int y, int width, int height) {
        fillRect(x + 6, y + 5, width - 12, height - 9, 7);
        drawDebriefFrame(x, y, width, height);
    }

    void drawDebriefFrame(int x, int y, int width, int height) {
        if (width < 30 || height < 24) {
            drawBox(x, y, width, height, 0);
            return;
        }

        const RectI topClip{x + 15, y, width - 30, 5};
        const RectI bottomClip{x + 15, y + height - 4, width - 30, 4};
        const RectI leftClip{x, y + 12, 6, height - 24};
        const RectI rightClip{x + width - 6, y + 12, 6, height - 24};

        for (int edgeX = topClip.x; edgeX < topClip.x + topClip.width; edgeX += 64) {
            drawImageAtClipped(gpicsArchive_, kDebriefFrameTopEdgeEntry, edgeX, topClip.y, true, topClip);
            drawImageAtClipped(gpicsArchive_, kDebriefFrameBottomEdgeEntry, edgeX, bottomClip.y, true, bottomClip);
        }
        for (int edgeY = leftClip.y; edgeY < leftClip.y + leftClip.height; edgeY += 32) {
            drawImageAtClipped(gpicsArchive_, kDebriefFrameLeftEdgeEntry, leftClip.x, edgeY, true, leftClip);
            drawImageAtClipped(gpicsArchive_, kDebriefFrameRightEdgeEntry, rightClip.x, edgeY, true, rightClip);
        }

        drawImageAt(gpicsArchive_, kDebriefFrameUpperLeftEntry, x, y, true);
        drawImageAt(gpicsArchive_, kDebriefFrameUpperRightEntry, x + width - 15, y, true);
        drawImageAt(gpicsArchive_, kDebriefFrameLowerLeftEntry, x, y + height - 12, true);
        drawImageAt(gpicsArchive_, kDebriefFrameLowerRightEntry, x + width - 15, y + height - 12, true);
    }

    void drawGpFrame(int x, int y, int width, int height) {
        const RectI topClip{x + 15, y, width - 30, 4};
        const RectI bottomClip{x + 15, y + height - 4, width - 30, 4};
        const RectI leftClip{x, y + 12, 6, height - 24};
        const RectI rightClip{x + width - 6, y + 12, 6, height - 24};

        for (int edgeX = topClip.x; edgeX < topClip.x + topClip.width; edgeX += 64) {
            drawImageAtClipped(gpicsArchive_, 22, edgeX, topClip.y, true, topClip);
            drawImageAtClipped(gpicsArchive_, 23, edgeX, bottomClip.y, true, bottomClip);
        }
        for (int edgeY = leftClip.y; edgeY < leftClip.y + leftClip.height; edgeY += 32) {
            drawImageAtClipped(gpicsArchive_, 24, leftClip.x, edgeY, true, leftClip);
            drawImageAtClipped(gpicsArchive_, 25, rightClip.x, edgeY, true, rightClip);
        }

        drawImageAt(gpicsArchive_, 18, x, y, true);
        drawImageAt(gpicsArchive_, 19, x + width - 15, y, true);
        drawImageAt(gpicsArchive_, 20, x, y + height - 12, true);
        drawImageAt(gpicsArchive_, 21, x + width - 15, y + height - 12, true);
    }

    void drawHeaderPanel(int x, int y, int width, std::wstring_view text) {
        constexpr int kHeaderTextBoxX = 17;
        constexpr int kHeaderTextBoxY = 15;
        constexpr int kHeaderTextBoxWidth = 110;
        constexpr int kHeaderTextBoxHeight = 12;
        const int textY = y + kHeaderTextBoxY + (kHeaderTextBoxHeight - smallFont_.height) / 2;
        if (!gpicsArchive_.images.empty()) {
            drawImageAt(gpicsArchive_, 4, x, y, true);
            drawSmallTextPlainCenteredInRect(x + kHeaderTextBoxX, textY, kHeaderTextBoxWidth, text, 15);
            return;
        }
        fillRect(x, y, width, 24, 0);
        drawBox(x - 2, y - 2, width + 4, 28, 7);
        drawSmallTextPlainCenteredInRect(x + kHeaderTextBoxX, textY, kHeaderTextBoxWidth, text, 15);
    }

    void drawCampaignButtons(bool introMessage) {
        (void)introMessage;
        drawImageAt(gpicsArchive_, 5, 25, 57, true);
        drawImageAt(gpicsArchive_, 6, 25, 106, true);
        if (contractAccepted_ || currentPlanetContractsAvailable()) {
            drawImageAt(
                gpicsArchive_,
                contractAccepted_ ? kMissionLaunchIconEntry : contractIconEntryForHouse(currentPlanetHouseId()),
                25,
                156,
                true);
        }

        if (contractAccepted_) {
            return;
        }
        drawImageAt(gpicsArchive_, 8, 257, 57, true);
        drawImageAt(gpicsArchive_, 9, 257, 106, true);
        drawImageAt(gpicsArchive_, 10, 257, 156, true);
    }

    void drawTextCentered(int y, std::wstring_view text, uint8_t color, uint8_t shadow) {
        const int width = textWidth(font_, text);
        drawText((kScreenWidth - width) / 2, y, text, color, shadow);
    }

    void drawSmallTextCentered(int y, std::wstring_view text, uint8_t color, uint8_t shadow) {
        const int width = textWidth(smallFont_, text);
        drawSmallText((kScreenWidth - width) / 2, y, text, color, shadow);
    }

    void drawTextCenteredInRect(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color,
        uint8_t shadow) {
        const int measuredWidth = textWidth(font_, text);
        drawText(x + (width - measuredWidth) / 2, y, text, color, shadow);
    }

    void drawSmallTextCenteredInRect(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color,
        uint8_t shadow) {
        const int measuredWidth = textWidth(smallFont_, text);
        drawSmallText(x + (width - measuredWidth) / 2, y, text, color, shadow);
    }

    void drawSmallTextPlainCenteredInRect(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = textWidth(smallFont_, text);
        drawTextPass(smallFont_, x + (width - measuredWidth) / 2, y, text, color);
    }

    void drawMenuTextCenteredInRect(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const Font& font = menuFont_.rows.empty() ? font_ : menuFont_;
        const int measuredWidth = textWidth(font, text);
        drawTextPass(font, x + (width - measuredWidth) / 2, y, text, color);
    }

    void drawTextPassCenteredInRect(
        const Font& font,
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = textWidth(font, text);
        drawTextPass(font, x + (width - measuredWidth) / 2, y, text, color);
    }

    template <size_t N>
    void drawMenu(int x, int y, const std::array<MenuItem, N>& items, size_t selectedIndex) {
        for (size_t i = 0; i < items.size(); ++i) {
            const int itemY = y + static_cast<int>(i) * 10;
            drawSmallText(x - 10, itemY, i == selectedIndex ? L">" : L" ", 15, 0);
            drawSmallText(x, itemY, items[i].label, i == selectedIndex ? 14 : 15, 0);
        }
    }

    void drawBarMenu(int x, int y, int width) {
        constexpr int kBarMenuLineStep = 10;
        for (size_t i = 0; i < kBarMenuItems.size(); ++i) {
            const int itemY = y + static_cast<int>(i) * kBarMenuLineStep;
            drawMenuTextCenteredInRect(x, itemY, width, kBarMenuItems[i].label, i == barMenuIndex_ ? 14 : 7);
        }
    }

    void drawMechLabMenu(int x, int y, int width) {
        constexpr int kMechLabMenuLineStep = 10;
        for (size_t i = 0; i < kMechLabMenuItems.size(); ++i) {
            const int itemY = y + static_cast<int>(i) * kMechLabMenuLineStep;
            drawMenuTextCenteredInRect(
                x,
                itemY,
                width,
                kMechLabMenuItems[i].label,
                i == mechLabMenuIndex_ ? 14 : 7);
        }
    }

    const OwnedMech* selectedOwnedMech() const {
        if (selectedMechIndex_ >= ownedMechs_.size()) {
            return nullptr;
        }
        return &ownedMechs_[selectedMechIndex_];
    }

    OwnedMech* selectedOwnedMech() {
        if (selectedMechIndex_ >= ownedMechs_.size()) {
            return nullptr;
        }
        return &ownedMechs_[selectedMechIndex_];
    }

    RectI mechReviewPanelRect() const {
        const size_t rowCount = std::min<size_t>(ownedMechs_.size(), kMaxOwnedMechs) + 1u;
        const int lastRowY = kMechReviewItemY + static_cast<int>(rowCount - 1u) * kMechReviewLineStep;
        const int height = (lastRowY + 14) - kMechReviewPanelY;
        return {kMechReviewPanelX, kMechReviewPanelY, kMechReviewPanelWidth, height};
    }

    MarketMech* selectedMarketMech() {
        PlanetMechMarket& market = currentPlanetMechMarket();
        if (selectedMarketMechIndex_ >= market.mechsForSale.size()) {
            return nullptr;
        }
        return &market.mechsForSale[selectedMarketMechIndex_];
    }

    const MarketMech* selectedMarketMech() const {
        if (planetMechMarkets_.empty()) {
            return nullptr;
        }
        const int index = planets_.empty()
            ? 0
            : std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        if (static_cast<size_t>(index) >= planetMechMarkets_.size()) {
            return nullptr;
        }
        const PlanetMechMarket& market = planetMechMarkets_[static_cast<size_t>(index)];
        if (selectedMarketMechIndex_ >= market.mechsForSale.size()) {
            return nullptr;
        }
        return &market.mechsForSale[selectedMarketMechIndex_];
    }

    size_t visibleMarketRows(size_t marketCount) const {
        return std::min<size_t>(
            marketCount > mechMarketScrollOffset_ ? marketCount - mechMarketScrollOffset_ : 0,
            kMechBuyListMaxVisibleRows);
    }

    int mechBuyListDoneY(size_t marketCount) const {
        const size_t visibleRows = visibleMarketRows(marketCount);
        const size_t rowSlots = std::max<size_t>(visibleRows, marketCount == 0 ? 6u : 0u);
        return kMechBuyListFirstRowY + static_cast<int>(rowSlots) * kMechBuyListLineStep;
    }

    RectI mechBuyListPanelRect() {
        const size_t marketCount = currentPlanetMechMarket().mechsForSale.size();
        if (marketCount == 0) {
            return kMechBuyMessagePanelRect;
        }
        const int doneY = mechBuyListDoneY(marketCount);
        const int height = (doneY + 14) - kMechBuyListPanelY;
        return {kMechBuyListPanelX, kMechBuyListPanelY, kMechBuyListPanelWidth, height};
    }

    void clampMechMarketScroll() {
        const PlanetMechMarket& market = currentPlanetMechMarket();
        const size_t count = market.mechsForSale.size();
        const size_t maxSelection = count;
        selectedMarketMechIndex_ = std::min(selectedMarketMechIndex_, maxSelection);

        if (selectedMarketMechIndex_ >= count) {
            const size_t visibleRows = std::min<size_t>(count, kMechBuyListMaxVisibleRows);
            mechMarketScrollOffset_ = count > visibleRows ? count - visibleRows : 0;
            return;
        }
        if (selectedMarketMechIndex_ < mechMarketScrollOffset_) {
            mechMarketScrollOffset_ = selectedMarketMechIndex_;
        } else if (selectedMarketMechIndex_ >= mechMarketScrollOffset_ + kMechBuyListMaxVisibleRows) {
            mechMarketScrollOffset_ = selectedMarketMechIndex_ + 1u - kMechBuyListMaxVisibleRows;
        }
    }

    std::wstring pilotNameForCrewSlot(int crewSlot) const {
        if (crewSlot >= 0 && static_cast<size_t>(crewSlot) < crewMembers_.size()) {
            const CrewMember& member = crewMembers_[static_cast<size_t>(crewSlot)];
            if (member.hired) {
                return std::wstring(member.name);
            }
        }
        return L"UNASSIGNED";
    }

    static bool sameRepairTarget(const RepairTarget& lhs, const RepairTarget& rhs) {
        return lhs.kind == rhs.kind && lhs.index == rhs.index;
    }

    static int armorDamageTotal(const OwnedMech& mech) {
        int total = 0;
        for (int value : mech.armorDamage) {
            total += std::clamp(value, 0, kArmorDamageMaxLevel);
        }
        return total;
    }

    static int armorDamagedSectionCount(const OwnedMech& mech) {
        int total = 0;
        for (int value : mech.armorDamage) {
            if (value > 0) {
                ++total;
            }
        }
        return total;
    }

    static int nextRepairableArmorDamageLevel(const OwnedMech& mech) {
        for (size_t section : kArmorDamageOrder) {
            if (section < mech.armorDamage.size() && mech.armorDamage[section] > 0) {
                return std::clamp(mech.armorDamage[section], 0, kArmorDamageMaxLevel);
            }
        }
        return 0;
    }

    static void updateArmorPercent(OwnedMech& mech) {
        const int damage = std::clamp(armorDamageTotal(mech), 0, kArmorDamageDenominator);
        mech.armorPercent = ((kArmorDamageDenominator - damage) * 100) / kArmorDamageDenominator;
    }

    static void damageArmorToPercent(OwnedMech& mech, int targetPercent) {
        const int clampedPercent = std::clamp(targetPercent, 0, 100);
        const int targetRemaining =
            (kArmorDamageDenominator * clampedPercent + 99) / 100;
        int damage = std::clamp(kArmorDamageDenominator - targetRemaining, 0, kArmorDamageDenominator);

        mech.armorDamage = {};
        for (size_t section : kArmorDamageOrder) {
            if (damage <= 0 || section >= mech.armorDamage.size()) {
                break;
            }
            const int applied = std::min(damage, kArmorDamageMaxLevel);
            mech.armorDamage[section] = applied;
            damage -= applied;
        }

        updateArmorPercent(mech);
    }

    static void repairArmorStep(OwnedMech& mech) {
        for (size_t section : kArmorDamageOrder) {
            if (section < mech.armorDamage.size() && mech.armorDamage[section] > 0) {
                mech.armorDamage[section] = 0;
                break;
            }
        }
        updateArmorPercent(mech);
    }

    static uint32_t damageRepairMultiplier(DamageState state) {
        switch (state) {
        case DamageState::Functional:
            return 0;
        case DamageState::LightDamage:
            return 1;
        case DamageState::HeavyDamage:
            return 2;
        case DamageState::Junk:
            return 3;
        }
        return 0;
    }

    DamageState componentCondition(const OwnedMech& mech, RepairTargetKind kind) const {
        switch (kind) {
        case RepairTargetKind::Engine:
            return mech.engine;
        case RepairTargetKind::Gyros:
            return mech.gyros;
        case RepairTargetKind::Sensors:
            return mech.sensors;
        case RepairTargetKind::LifeSupport:
            return mech.lifeSupport;
        case RepairTargetKind::LeftArmActuator:
            return mech.leftArmActuator;
        case RepairTargetKind::RightArmActuator:
            return mech.rightArmActuator;
        case RepairTargetKind::LeftLegActuator:
            return mech.leftLegActuator;
        case RepairTargetKind::RightLegActuator:
            return mech.rightLegActuator;
        default:
            return DamageState::Functional;
        }
    }

    void setComponentCondition(OwnedMech& mech, RepairTargetKind kind, DamageState state) {
        switch (kind) {
        case RepairTargetKind::Engine:
            mech.engine = state;
            break;
        case RepairTargetKind::Gyros:
            mech.gyros = state;
            break;
        case RepairTargetKind::Sensors:
            mech.sensors = state;
            break;
        case RepairTargetKind::LifeSupport:
            mech.lifeSupport = state;
            break;
        case RepairTargetKind::LeftArmActuator:
            mech.leftArmActuator = state;
            break;
        case RepairTargetKind::RightArmActuator:
            mech.rightArmActuator = state;
            break;
        case RepairTargetKind::LeftLegActuator:
            mech.leftLegActuator = state;
            break;
        case RepairTargetKind::RightLegActuator:
            mech.rightLegActuator = state;
            break;
        default:
            break;
        }
    }

    static uint32_t componentBaseCost(const OwnedMech& mech, RepairTargetKind kind) {
        static constexpr std::array<std::array<uint32_t, 8>, kPlayableMechCount> costs = {{
            {{210, 190, 40, 50, 5, 5, 7, 7}},
            {{0, 0, 40, 50, 5, 5, 7, 7}},
            {{570, 540, 70, 50, 8, 8, 12, 12}},
            {{42, 2, 90, 50, 10, 10, 15, 15}},
            {{232, 132, 110, 50, 13, 13, 19, 19}},
            {{240, 142, 110, 50, 13, 13, 19, 19}},
            {{192, 112, 120, 50, 14, 14, 21, 21}},
            {{20, 176, 140, 50, 16, 16, 24, 24}},
            {{220, 20, 150, 50, 17, 17, 26, 26}},
            {{108, 64, 170, 50, 19, 19, 29, 29}},
        }};

        size_t componentIndex = 0;
        switch (kind) {
        case RepairTargetKind::Engine:
            componentIndex = 0;
            break;
        case RepairTargetKind::Gyros:
            componentIndex = 1;
            break;
        case RepairTargetKind::Sensors:
            componentIndex = 2;
            break;
        case RepairTargetKind::LifeSupport:
            componentIndex = 3;
            break;
        case RepairTargetKind::LeftArmActuator:
            componentIndex = 4;
            break;
        case RepairTargetKind::RightArmActuator:
            componentIndex = 5;
            break;
        case RepairTargetKind::LeftLegActuator:
            componentIndex = 6;
            break;
        case RepairTargetKind::RightLegActuator:
            componentIndex = 7;
            break;
        default:
            return 0;
        }
        return costs[chassisIndex(mech.chassis)][componentIndex];
    }

    uint8_t currentPlanetEconomyTier() const {
        if (planets_.empty()) {
            return 1;
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return std::clamp<uint8_t>(planets_[static_cast<size_t>(index)].economyTier, 1, 4);
    }

    uint32_t mechBuyPrice(const OwnedMech& mech) const {
        const uint8_t tier = currentPlanetEconomyTier();
        const uint32_t basePrice = mechDefinition(mech.chassis).buyPrices[tier - 1u];
        const uint32_t repairCost = totalRepairCost(mech);
        return basePrice > repairCost ? basePrice - repairCost : 0;
    }

    uint32_t mechSellOffer(const OwnedMech& mech) const {
        const uint8_t tier = currentPlanetEconomyTier();
        const uint32_t basePrice = mechDefinition(mech.chassis).sellPrices[tier - 1u];
        const uint32_t repairCost = totalRepairCost(mech);
        return basePrice > repairCost ? basePrice - repairCost : 0;
    }

    PlanetMechMarket& currentPlanetMechMarket() {
        if (planetMechMarkets_.size() != planets_.size()) {
            planetMechMarkets_.assign(planets_.size(), {});
        }
        const int index = planets_.empty()
            ? 0
            : std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        if (planetMechMarkets_.empty()) {
            planetMechMarkets_.push_back({});
        }
        return planetMechMarkets_[static_cast<size_t>(index)];
    }

    void ensureCurrentPlanetMechMarket() {
        PlanetMechMarket& market = currentPlanetMechMarket();
        if (market.initialized) {
            return;
        }

        market.initialized = true;
        std::mt19937 rng(makePlanetMarketSeed());
        const uint8_t tier = currentPlanetEconomyTier();
        const int count = generateMarketMechCount(tier, rng);
        market.mechsForSale.reserve(market.mechsForSale.size() + static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            OwnedMech mech = makeMech(randomMarketChassis(rng), -1);
            randomizeMarketMech(mech, rng);
            market.mechsForSale.push_back({mech, mechBuyPrice(mech)});
        }
    }

    uint32_t makePlanetMarketSeed() const {
        uint32_t seed = 0x4D574D4Bu;
        seed ^= static_cast<uint32_t>(std::max(0, currentPlanetIndex_)) * 0x9E3779B9u;
        seed ^= static_cast<uint32_t>(currentYear_) * 0x85EBCA6Bu;
        seed ^= static_cast<uint32_t>(currentMonth_ + 1) * 0xC2B2AE35u;
        seed ^= static_cast<uint32_t>(currentDay_) * 0x27D4EB2Fu;
        seed ^= static_cast<uint32_t>(currentPlanetVisitSerial_) * 0x165667B1u;
        return seed;
    }

    static int generateMarketMechCount(uint8_t tier, std::mt19937& rng) {
        const uint32_t roll = rng() % 100u;
        switch (std::clamp<uint8_t>(tier, 1, 4)) {
        case 1:
            if (roll < 8) {
                return 0;
            }
            return 6 + static_cast<int>(rng() % 7u);
        case 2:
            if (roll < 12) {
                return 0;
            }
            return 4 + static_cast<int>(rng() % 5u);
        case 3:
            if (roll < 20) {
                return 0;
            }
            return 2 + static_cast<int>(rng() % 3u);
        case 4:
        default:
            if (roll < 30) {
                return 0;
            }
            return 1 + static_cast<int>(rng() % 3u);
        }
    }

    static ChassisId randomMarketChassis(std::mt19937& rng) {
        uint32_t totalWeight = 0;
        for (const MechDefinition& definition : kMechDefinitions) {
            totalWeight += definition.marketWeight;
        }
        uint32_t roll = totalWeight == 0 ? 0 : rng() % totalWeight;
        for (const MechDefinition& definition : kMechDefinitions) {
            if (roll < definition.marketWeight) {
                return definition.chassis;
            }
            roll -= definition.marketWeight;
        }
        return ChassisId::Locust;
    }

    void randomizeMarketMech(OwnedMech& mech, std::mt19937& rng) {
        std::uniform_int_distribution<int> ammoDist(0, kMechAmmoMaxPacks);
        const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
        for (size_t i = 0; i < ammoTypes.size(); ++i) {
            if (ammoTypes[i]) {
                mech.ammoPacks[i] = ammoDist(rng);
            }
        }

        const uint32_t roll = rng() % 100u;
        if (roll < 28) {
            mech.repairCost = 0;
            mech.condition = DamageState::Functional;
            return;
        }

        DamageState targetState = DamageState::LightDamage;
        int componentHits = 2;
        int weaponHits = 1;
        int armorMin = 70;
        int armorPercentMax = 94;
        if (roll >= 76 && roll < 94) {
            targetState = DamageState::HeavyDamage;
            componentHits = 4;
            weaponHits = 2;
            armorMin = 35;
            armorPercentMax = 72;
        } else if (roll >= 94) {
            targetState = DamageState::Junk;
            componentHits = 6;
            weaponHits = 3;
            armorMin = 12;
            armorPercentMax = 45;
        }

        static constexpr std::array<RepairTargetKind, 8> componentKinds = {{
            RepairTargetKind::Engine,
            RepairTargetKind::Gyros,
            RepairTargetKind::Sensors,
            RepairTargetKind::LifeSupport,
            RepairTargetKind::LeftArmActuator,
            RepairTargetKind::RightArmActuator,
            RepairTargetKind::LeftLegActuator,
            RepairTargetKind::RightLegActuator,
        }};

        for (int i = 0; i < componentHits; ++i) {
            setComponentCondition(mech, componentKinds[rng() % componentKinds.size()], targetState);
        }
        for (int i = 0; i < weaponHits; ++i) {
            damageRandomWeapon(mech, targetState, rng);
        }

        mech.heatSinksWorking = std::max(0, mech.heatSinksTotal - static_cast<int>(rng() % (targetState == DamageState::Junk ? 6u : 3u)));
        mech.jumpJetsWorking = std::max(0, mech.jumpJetsTotal - static_cast<int>(rng() % (targetState == DamageState::Functional ? 1u : 4u)));
        std::uniform_int_distribution<int> armorDist(armorMin, armorPercentMax);
        damageArmorToPercent(mech, armorDist(rng));

        mech.repairCost = totalRepairCost(mech);
        mech.condition = targetState == DamageState::Junk ? DamageState::Junk : DamageState::Functional;
    }

    static void damageRandomWeapon(OwnedMech& mech, DamageState state, std::mt19937& rng) {
        std::vector<size_t> weaponIndexes;
        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            if (!mech.weapons[i].weapon.empty()) {
                weaponIndexes.push_back(i);
            }
        }
        if (weaponIndexes.empty()) {
            return;
        }
        mech.weapons[weaponIndexes[rng() % weaponIndexes.size()]].condition = state;
    }

    uint32_t reloadCost(const OwnedMech& mech) const {
        uint32_t total = 0;
        const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
        for (size_t i = 0; i < ammoTypes.size() && i < kAmmoDefinitions.size(); ++i) {
            if (!ammoTypes[i]) {
                continue;
            }
            const int missing = std::max(0, kMechAmmoMaxPacks - mech.ammoPacks[i]);
            total += static_cast<uint32_t>(missing) * kAmmoDefinitions[i].tierOneCost;
        }
        return total;
    }

    uint32_t extraAmmoCost(size_t ammoIndex) const {
        if (ammoIndex >= kAmmoDefinitions.size()) {
            return 0;
        }

        constexpr uint32_t planetTier = 1;
        return kAmmoDefinitions[ammoIndex].tierOneCost * planetTier;
    }

    uint32_t repairTargetCost(const OwnedMech& mech, const RepairTarget& target) const {
        switch (target.kind) {
        case RepairTargetKind::Engine:
        case RepairTargetKind::Gyros:
        case RepairTargetKind::Sensors:
        case RepairTargetKind::LifeSupport:
        case RepairTargetKind::LeftArmActuator:
        case RepairTargetKind::RightArmActuator:
        case RepairTargetKind::LeftLegActuator:
        case RepairTargetKind::RightLegActuator:
            return componentBaseCost(mech, target.kind) *
                kMechRepairComponentMultiplier *
                damageRepairMultiplier(componentCondition(mech, target.kind));
        case RepairTargetKind::HeatSink:
            return static_cast<uint32_t>(std::max(0, mech.heatSinksTotal - mech.heatSinksWorking)) *
                kMechRepairCountUnitCost;
        case RepairTargetKind::JumpJets:
            return static_cast<uint32_t>(std::max(0, mech.jumpJetsTotal - mech.jumpJetsWorking)) *
                kMechRepairCountUnitCost;
        case RepairTargetKind::Armor:
            return static_cast<uint32_t>(nextRepairableArmorDamageLevel(mech)) *
                kMechRepairArmorLevelCost;
        case RepairTargetKind::Weapon:
            if (target.index < mech.weapons.size()) {
                return kMechRepairWeaponLightCost * damageRepairMultiplier(mech.weapons[target.index].condition);
            }
            return 0;
        }
        return 0;
    }

    std::vector<RepairTarget> repairableTargets(const OwnedMech& mech) const {
        std::vector<RepairTarget> targets;
        const auto addComponent = [&](RepairTargetKind kind) {
            RepairTarget target{kind, 0};
            if (repairTargetCost(mech, target) > 0) {
                targets.push_back(target);
            }
        };

        addComponent(RepairTargetKind::Engine);
        addComponent(RepairTargetKind::Gyros);
        addComponent(RepairTargetKind::Sensors);
        addComponent(RepairTargetKind::LifeSupport);
        if (mech.heatSinksWorking < mech.heatSinksTotal) {
            targets.push_back({RepairTargetKind::HeatSink, 0});
        }
        addComponent(RepairTargetKind::LeftArmActuator);
        addComponent(RepairTargetKind::RightArmActuator);
        addComponent(RepairTargetKind::LeftLegActuator);
        addComponent(RepairTargetKind::RightLegActuator);
        if (mech.jumpJetsWorking < mech.jumpJetsTotal) {
            targets.push_back({RepairTargetKind::JumpJets, 0});
        }
        if (armorDamagedSectionCount(mech) > 0) {
            targets.push_back({RepairTargetKind::Armor, 0});
        }
        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            if (!mech.weapons[i].weapon.empty() && mech.weapons[i].condition != DamageState::Functional) {
                targets.push_back({RepairTargetKind::Weapon, i});
            }
        }
        return targets;
    }

    uint32_t totalRepairCost(const OwnedMech& mech) const {
        uint32_t total = 0;
        for (const RepairTarget& target : repairableTargets(mech)) {
            if (target.kind == RepairTargetKind::Armor) {
                total += static_cast<uint32_t>(armorDamageTotal(mech)) *
                    kMechRepairArmorLevelCost;
            } else {
                total += repairTargetCost(mech, target);
            }
        }
        return total;
    }

    bool selectedRepairTarget(const OwnedMech& mech, RepairTarget& target) const {
        const std::vector<RepairTarget> targets = repairableTargets(mech);
        if (targets.empty()) {
            return false;
        }
        target = targets[std::min(repairSelectionIndex_, targets.size() - 1u)];
        return true;
    }

    bool isSelectedRepairTarget(const OwnedMech& mech, RepairTarget target) const {
        RepairTarget selected;
        return selectedRepairTarget(mech, selected) && sameRepairTarget(selected, target);
    }

    uint8_t repairLineColor(const OwnedMech& mech, RepairTarget target, bool needsRepair) const {
        if (needsRepair && isSelectedRepairTarget(mech, target)) {
            return 14;
        }
        return needsRepair ? 12 : 7;
    }

    uint32_t selectedRepairCost(const OwnedMech& mech) const {
        RepairTarget selected;
        return selectedRepairTarget(mech, selected) ? repairTargetCost(mech, selected) : 0;
    }

    void randomizeStartingMechDamage(OwnedMech& mech) {
        std::mt19937 rng(
            static_cast<uint32_t>(GetTickCount()) ^
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mech)));

        damageRandomWeapon(mech, DamageState::LightDamage, rng);

        static constexpr std::array<RepairTargetKind, 8> componentKinds = {{
            RepairTargetKind::Engine,
            RepairTargetKind::Gyros,
            RepairTargetKind::Sensors,
            RepairTargetKind::LifeSupport,
            RepairTargetKind::LeftArmActuator,
            RepairTargetKind::RightArmActuator,
            RepairTargetKind::LeftLegActuator,
            RepairTargetKind::RightLegActuator,
        }};
        std::uniform_int_distribution<size_t> componentDist(0, componentKinds.size() - 1u);
        setComponentCondition(mech, componentKinds[componentDist(rng)], DamageState::LightDamage);

        std::uniform_int_distribution<int> heatDist(1, 2);
        mech.heatSinksWorking = std::max(0, mech.heatSinksTotal - heatDist(rng));

        std::uniform_int_distribution<int> jumpDist(0, 1);
        mech.jumpJetsWorking = std::max(0, mech.jumpJetsTotal - jumpDist(rng));

        std::uniform_int_distribution<int> armorDist(55, 90);
        damageArmorToPercent(mech, armorDist(rng));

        mech.repairCost = totalRepairCost(mech);
        mech.condition = DamageState::Functional;
    }

    bool repairTargetAtScreenY(const OwnedMech& mech, int screenY, RepairTarget& target) const {
        static constexpr std::array<RepairTargetKind, 10> componentRows = {{
            RepairTargetKind::Engine,
            RepairTargetKind::Gyros,
            RepairTargetKind::Sensors,
            RepairTargetKind::LifeSupport,
            RepairTargetKind::HeatSink,
            RepairTargetKind::LeftArmActuator,
            RepairTargetKind::RightArmActuator,
            RepairTargetKind::LeftLegActuator,
            RepairTargetKind::RightLegActuator,
            RepairTargetKind::JumpJets,
        }};

        for (size_t i = 0; i < componentRows.size(); ++i) {
            const int y = kMechRepairFirstLineY + static_cast<int>(i) * kMechRepairComponentLineStep;
            if (screenY >= y && screenY < y + kMechRepairComponentLineStep) {
                target = {componentRows[i], 0};
                return repairTargetCost(mech, target) > 0;
            }
        }

        const int armorY = kMechRepairFirstLineY +
            static_cast<int>(componentRows.size()) * kMechRepairComponentLineStep;
        if (screenY >= armorY && screenY < armorY + kMechRepairComponentLineStep) {
            target = {RepairTargetKind::Armor, 0};
            return repairTargetCost(mech, target) > 0;
        }

        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            const int y = kMechRepairWeaponFirstRowY + static_cast<int>(i) * kMechRepairComponentLineStep;
            if (screenY >= y && screenY < y + kMechRepairComponentLineStep) {
                target = {RepairTargetKind::Weapon, i};
                return repairTargetCost(mech, target) > 0;
            }
        }
        return false;
    }

    static std::wstring_view damageLabel(DamageState state) {
        switch (state) {
        case DamageState::Functional:
            return L"FUNCTIONAL";
        case DamageState::LightDamage:
            return L"LIGHT DAMAGE";
        case DamageState::HeavyDamage:
            return L"HEAVY DAMAGE";
        case DamageState::Junk:
            return L"NONFUNCTIONAL";
        }
        return L"FUNCTIONAL";
    }

    static std::wstring_view mechConditionLabel(const OwnedMech& mech) {
        return mech.condition == DamageState::Junk ? L"NONFUNCTIONAL" : L"FUNCTIONAL";
    }

    static uint8_t mechConditionColor(const OwnedMech& mech) {
        (void)mech;
        return 12;
    }

    static uint8_t damageColor(DamageState state) {
        switch (state) {
        case DamageState::Functional:
            return 7;
        case DamageState::LightDamage:
            return 14;
        case DamageState::HeavyDamage:
            return 12;
        case DamageState::Junk:
            return 8;
        }
        return 7;
    }

    static uint8_t countConditionColor(int working, int total) {
        if (total <= 0 || working >= total) {
            return 7;
        }
        if (working * 2 >= total) {
            return 14;
        }
        return 12;
    }

    static uint8_t damageLevelConditionColor(DamageState state) {
        return state == DamageState::Functional ? 7 : 12;
    }

    static uint8_t damageLevelCountColor(int working, int total) {
        return total <= 0 || working >= total ? 7 : 12;
    }

    static uint8_t damageLevelArmorColor(const OwnedMech& mech) {
        return mech.armorPercent >= 100 ? 7 : 12;
    }

    static uint8_t armorColor(const OwnedMech& mech) {
        if (mech.armorPercent >= 90) {
            return 7;
        }
        if (mech.armorPercent >= 35) {
            return 14;
        }
        return 12;
    }

    void drawMechStatusText(int x, int y, std::wstring_view text, uint8_t color) {
        if (smallFont_.rows.empty()) {
            return;
        }

        int cursorX = x;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            drawGlyph5x5(smallFont_, cursorX, y, ch, color);
            cursorX += 6;
        }
    }

    int mechStatusTextWidth(std::wstring_view text) const {
        return static_cast<int>(text.size()) * 6;
    }

    void drawMechStatusTextCentered(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = mechStatusTextWidth(text);
        drawMechStatusText(x + (width - measuredWidth) / 2, y, text, color);
    }

    void drawMechStatusTextRightAligned(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = mechStatusTextWidth(text);
        drawMechStatusText(x + std::max(0, width - measuredWidth), y, text, color);
    }

    void drawMechStatusInline(
        int x,
        int y,
        std::wstring_view label,
        std::wstring_view value,
        uint8_t valueColor) {
        drawMechStatusText(x, y, label, 7);
        drawMechStatusText(x + mechStatusTextWidth(label), y, value, valueColor);
    }

    void drawRepairCostLine(uint32_t repairCost) {
        drawMechStatusText(12, kMechRepairCostY, L"COST OF REPAIR:", 7);
        drawMechStatusTextRightAligned(
            kMechRepairCostValueX,
            kMechRepairCostY,
            kMechRepairCostValueWidth,
            formatWealth(repairCost),
            7);
    }

    void drawMechStatusFrame() {
        drawGpPanel(0, 0, kScreenWidth, kScreenHeight, 8);
        drawMechStatusGrid();
        drawGpFrame(0, 0, kScreenWidth, kScreenHeight);
    }

    void drawMechStatusGrid() {
        const int gridStep = kMechStatusGridCellSize + kMechStatusGridLineSize;
        const int gridWidth =
            kMechStatusGridColumns * kMechStatusGridCellSize +
            (kMechStatusGridColumns - 1) * kMechStatusGridLineSize;
        const int gridHeight =
            kMechStatusGridLineSize +
            kMechStatusGridRows * kMechStatusGridCellSize +
            (kMechStatusGridRows - 1) * kMechStatusGridLineSize;

        fillRect(kMechStatusGridX, kMechStatusGridY, gridWidth, gridHeight, 8);
        for (int row = 0; row < kMechStatusGridRows; ++row) {
            const int cellY = kMechStatusGridY + kMechStatusGridLineSize + row * gridStep;
            for (int col = 0; col < kMechStatusGridColumns; ++col) {
                const int cellX = kMechStatusGridX + col * gridStep;
                fillRect(cellX, cellY, kMechStatusGridCellSize, kMechStatusGridCellSize, 9);
            }
        }
    }

    void drawMechStatusLine(
        int x,
        int y,
        std::wstring_view label,
        std::wstring_view value,
        uint8_t valueColor) {
        drawMechStatusText(x, y, label, 7);
        drawMechStatusText(x + 82, y, value, valueColor);
    }

    void drawMechStatusMenu() {
        static constexpr std::array<std::wstring_view, 5> labels = {{
            L"REPAIR",
            L"REPAIR ALL",
            L"RELOAD",
            L"SELL",
            L"DONE",
        }};

        int y = 150;
        for (size_t i = 0; i < labels.size(); ++i) {
            drawMechStatusText(58, y, labels[i], i == mechStatusMenuIndex_ ? 14 : 7);
            y += 8;
        }
    }

    void drawMechAmmoLines(const OwnedMech& mech, int y) {
        const std::array<bool, 6> ammoTypes = ammoTypesForMech(mech);
        for (size_t i = 0; i < ammoTypes.size() && i < kAmmoDefinitions.size(); ++i) {
            if (!ammoTypes[i]) {
                continue;
            }
            std::wstring line(kAmmoDefinitions[i].label);
            line += L"  :";
            line += std::to_wstring(mech.ammoPacks[i]);
            drawMechStatusText(kMechStatusTextX + 12, y, line, 12);
            y += 8;
        }
    }

    void drawMechBuyStatusMenu() {
        static constexpr std::array<std::wstring_view, 3> labels = {{
            L"BUY",
            L"DAMAGE LEVELS",
            L"DONE",
        }};

        int y = kMechBuyMenuFirstY;
        for (size_t i = 0; i < labels.size(); ++i) {
            drawMechStatusText(58, y, labels[i], i == mechBuyMenuIndex_ ? 14 : 7);
            y += kMechBuyMenuLineStep;
        }
    }

    void drawMechStatusImage(const OwnedMech& mech) {
        const RectI imageRect = mechStatusImageRect(mech);
        drawImageAt(mechStatusArchive_, mech.statusImageEntry, imageRect.x, imageRect.y, false);
        drawArmorOverlay(mech, imageRect.x, imageRect.y);
        drawGpFrame(0, 0, kScreenWidth, kScreenHeight);
    }

    static RectI mechStatusImageRect(const OwnedMech& mech) {
        const MechDefinition& definition = mechDefinition(mech.chassis);
        const int gridWidth = kScreenWidth - kMechStatusGridX;
        const RectI offset = mechStatusImageOffset(mech.chassis);
        return {
            kMechStatusGridX + std::max(0, (gridWidth - definition.statusImage.width) / 2) + offset.x,
            std::max(0, (kScreenHeight - definition.statusImage.height) / 2) + offset.y,
            definition.statusImage.width,
            definition.statusImage.height,
        };
    }

    static RectI mechStatusImageOffset(ChassisId chassis) {
        switch (chassis) {
        case ChassisId::Locust:
        case ChassisId::Wasp:
            return {1, -3, 0, 0};
        case ChassisId::Jenner:
            return {0, -2, 0, 0};
        case ChassisId::PhoenixHawk:
            return {1, -1, 0, 0};
        case ChassisId::ShadowHawk:
        case ChassisId::Wolverine:
            return {1, -3, 0, 0};
        case ChassisId::Rifleman:
            return {0, -1, 0, 0};
        case ChassisId::Warhammer:
            return {-1, -3, 0, 0};
        case ChassisId::Marauder:
            return {-1, -2, 0, 0};
        case ChassisId::Battlemaster:
            return {0, -3, 0, 0};
        }
        return {};
    }

    static uint8_t armorSectionColor(int damageLevel) {
        switch (std::clamp(damageLevel, 0, kArmorDamageMaxLevel)) {
        case 0:
            return 7;
        case 1:
            return 14;
        case 2:
            return 12;
        case 3:
            return 0;
        }
        return 7;
    }

    static size_t statusArtSectionToArmorDamageIndex(size_t statusArtSection) {
        switch (statusArtSection) {
        case 5:
            return 0; // RA
        case 4:
            return 1; // LA
        case 7:
            return 2; // RL
        case 6:
            return 3; // LL
        case 0:
            return 4; // HEAD
        case 1:
            return 5; // CT
        case 8:
            return 6; // BACK / center rear
        case 3:
        case 10:
            return 7; // TR
        case 2:
        case 9:
            return 8; // TL
        }
        return kArmorSectionCount;
    }

    static size_t armorOverlayIndex(ChassisId chassis) {
        switch (chassis) {
        case ChassisId::Locust:
        case ChassisId::Wasp:
            return 0;
        case ChassisId::Jenner:
            return 1;
        case ChassisId::PhoenixHawk:
            return 2;
        case ChassisId::ShadowHawk:
        case ChassisId::Wolverine:
            return 3;
        case ChassisId::Rifleman:
            return 4;
        case ChassisId::Warhammer:
            return 5;
        case ChassisId::Marauder:
            return 6;
        case ChassisId::Battlemaster:
            return 7;
        }
        return 0;
    }

    static const std::vector<ArmorOverlayRect>& armorOverlayRects(ChassisId chassis) {
        static constexpr size_t kMechStatusOverlayCount = 8;
        static const std::array<std::vector<ArmorOverlayRect>, kMechStatusOverlayCount> overlays = {{
            {
                {5, {0, 2, 17, 29}},
                {7, {6, 139, 45, 43}},
                {7, {14, 67, 31, 82}},
                {3, {15, 6, 23, 62}},
                {1, {36, 7, 40, 35}},
                {1, {36, 39, 32, 18}},
                {1, {46, 54, 14, 14}},
                {1, {49, 63, 12, 28}},
                {2, {74, 2, 22, 58}},
                {2, {66, 40, 12, 19}},
                {6, {67, 66, 35, 116}},
                {6, {61, 160, 45, 22}},
                {4, {99, 2, 12, 30}},
            },
            {
                {5, {0, 14, 16, 29}},
                {7, {20, 18, 16, 40}},
                {7, {18, 58, 24, 105}},
                {3, {35, 3, 28, 19}},
                {1, {36, 21, 52, 27}},
                {2, {62, 0, 26, 22}},
                {6, {87, 12, 20, 56}},
                {6, {87, 68, 21, 96}},
                {4, {108, 13, 17, 30}},
            },
            {
                {5, {0, 18, 23, 108}},
                {7, {22, 82, 28, 101}},
                {3, {21, 31, 15, 32}},
                {3, {28, 49, 22, 35}},
                {1, {34, 0, 47, 33}},
                {1, {38, 30, 41, 19}},
                {1, {48, 48, 18, 70}},
                {2, {78, 32, 17, 34}},
                {2, {64, 48, 16, 34}},
                {6, {65, 82, 30, 101}},
                {4, {93, 8, 22, 112}},
            },
            {
                {5, {0, 0, 24, 121}},
                {7, {21, 135, 35, 47}},
                {7, {26, 102, 27, 35}},
                {7, {28, 93, 7, 13}},
                {7, {31, 96, 14, 11}},
                {3, {22, 0, 24, 96}},
                {1, {44, 0, 31, 104}},
                {2, {73, 0, 25, 98}},
                {6, {69, 99, 20, 13}},
                {6, {80, 96, 7, 7}},
                {6, {64, 110, 29, 45}},
                {6, {61, 153, 39, 29}},
                {4, {96, 0, 23, 115}},
            },
            {
                {5, {0, 11, 27, 61}},
                {5, {5, 64, 19, 81}},
                {7, {22, 82, 37, 105}},
                {3, {25, 9, 31, 65}},
                {1, {33, 0, 72, 8}},
                {1, {54, 7, 26, 73}},
                {1, {43, 73, 44, 10}},
                {1, {57, 77, 23, 41}},
                {2, {79, 7, 28, 76}},
                {6, {78, 81, 35, 106}},
                {6, {71, 165, 15, 21}},
                {4, {106, 5, 29, 73}},
                {4, {113, 72, 22, 73}},
            },
            {
                {5, {0, 73, 20, 18}},
                {5, {0, 33, 20, 39}},
                {7, {8, 127, 47, 51}},
                {7, {20, 88, 28, 45}},
                {3, {0, 0, 40, 35}},
                {3, {21, 34, 18, 52}},
                {1, {48, 84, 15, 21}},
                {1, {38, 3, 39, 67}},
                {1, {38, 68, 39, 22}},
                {2, {75, 0, 20, 88}},
                {2, {91, 3, 22, 20}},
                {6, {70, 89, 38, 89}},
                {6, {62, 131, 18, 47}},
                {4, {93, 27, 20, 64}},
            },
            {
                {5, {0, 51, 23, 62}},
                {7, {28, 58, 23, 34}},
                {7, {26, 86, 26, 22}},
                {7, {7, 141, 53, 20}},
                {7, {27, 101, 20, 47}},
                {3, {21, 0, 24, 60}},
                {1, {43, 0, 35, 59}},
                {2, {76, 0, 24, 60}},
                {6, {69, 59, 24, 50}},
                {6, {71, 102, 24, 24}},
                {6, {66, 121, 51, 40}},
                {4, {98, 39, 23, 71}},
            },
            {
                {5, {1, 18, 23, 20}},
                {5, {20, 35, 5, 7}},
                {5, {0, 35, 22, 30}},
                {5, {3, 60, 21, 60}},
                {3, {23, 11, 26, 83}},
                {3, {24, 90, 8, 12}},
                {1, {48, 0, 29, 90}},
                {1, {54, 88, 8, 20}},
                {2, {75, 8, 24, 85}},
                {2, {95, 26, 7, 9}},
                {6, {71, 94, 27, 28}},
                {6, {67, 119, 42, 62}},
                {6, {32, 93, 22, 29}},
                {6, {29, 117, 22, 14}},
                {6, {22, 124, 35, 57}},
                {4, {102, 13, 23, 104}},
                {4, {100, 77, 10, 43}},
                {4, {99, 35, 9, 46}},
            },
        }};
        return overlays[armorOverlayIndex(chassis)];
    }

    void drawArmorOverlay(const OwnedMech& mech, int imageX, int imageY) {
        const uint32_t magenta = toBgra(paletteColor(5));
        const uint32_t brightMagenta = toBgra(paletteColor(13));

        for (const ArmorOverlayRect& overlayRect : armorOverlayRects(mech.chassis)) {
            const size_t section = statusArtSectionToArmorDamageIndex(overlayRect.section);
            if (section >= mech.armorDamage.size()) {
                continue;
            }

            const RectI& imageRect = overlayRect.rect;
            const uint32_t target =
                toBgra(paletteColor(armorSectionColor(mech.armorDamage[section])));
            const int x0 = std::max(0, imageX + imageRect.x);
            const int y0 = std::max(0, imageY + imageRect.y);
            const int x1 = std::min(kScreenWidth, imageX + imageRect.x + imageRect.width);
            const int y1 = std::min(kScreenHeight, imageY + imageRect.y + imageRect.height);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    uint32_t& pixel = framebuffer_[static_cast<size_t>(y) * kScreenWidth + x];
                    if (pixel == magenta || pixel == brightMagenta) {
                        pixel = target;
                    }
                }
            }
        }
    }

    void drawRepairComponentLine(
        const OwnedMech& mech,
        int y,
        std::wstring_view label,
        RepairTargetKind kind,
        DamageState condition) {
        const RepairTarget target{kind, 0};
        const bool needsRepair = repairTargetCost(mech, target) > 0;
        const uint8_t color = repairLineColor(mech, target, needsRepair);
        drawMechStatusText(12, y, label, color);
        drawMechStatusText(90, y, damageLabel(condition), color);
    }

    void drawRepairCountLine(
        const OwnedMech& mech,
        int y,
        std::wstring_view label,
        RepairTargetKind kind,
        int working,
        int total) {
        const RepairTarget target{kind, 0};
        const bool needsRepair = repairTargetCost(mech, target) > 0;
        const uint8_t color = repairLineColor(mech, target, needsRepair);
        drawMechStatusText(12, y, label, color);
        drawMechStatusText(90, y, std::to_wstring(working) + L" OF " + std::to_wstring(total), color);
    }

    void drawRepairArmorLine(const OwnedMech& mech, int y) {
        const RepairTarget target{RepairTargetKind::Armor, 0};
        const bool needsRepair = repairTargetCost(mech, target) > 0;
        const uint8_t color = repairLineColor(mech, target, needsRepair);
        drawMechStatusText(12, y, L"ARMOR:", color);
        drawMechStatusText(90, y, std::to_wstring(mech.armorPercent) + L" %", color);
    }

    void drawRepairWeaponTable(const OwnedMech& mech) {
        static constexpr int kWeaponColumnX = 12;
        static constexpr int kLocationColumnX = 54;
        static constexpr int kConditionColumnX = 90;

        drawMechStatusText(kWeaponColumnX, kMechRepairWeaponHeaderY, L"WPN", 7);
        drawMechStatusText(kLocationColumnX, kMechRepairWeaponHeaderY, L"LOC", 7);
        drawMechStatusText(kConditionColumnX, kMechRepairWeaponHeaderY, L"CONDITION", 7);

        int y = kMechRepairWeaponFirstRowY;
        for (size_t i = 0; i < mech.weapons.size(); ++i) {
            const MechWeaponStatus& weapon = mech.weapons[i];
            if (weapon.weapon.empty()) {
                continue;
            }
            const RepairTarget target{RepairTargetKind::Weapon, i};
            const bool needsRepair = repairTargetCost(mech, target) > 0;
            const uint8_t color = repairLineColor(mech, target, needsRepair);
            drawMechStatusText(kWeaponColumnX, y, weapon.weapon, color);
            drawMechStatusText(kLocationColumnX, y, weapon.location, color);
            drawMechStatusText(kConditionColumnX, y, damageLabel(weapon.condition), color);
            y += kMechRepairComponentLineStep;
        }
    }

    void drawDamageComponentLine(int y, std::wstring_view label, DamageState condition) {
        const uint8_t color = damageLevelConditionColor(condition);
        drawMechStatusText(12, y, label, color);
        drawMechStatusText(90, y, damageLabel(condition), color);
    }

    void drawDamageCountLine(int y, std::wstring_view label, int working, int total) {
        const uint8_t color = damageLevelCountColor(working, total);
        drawMechStatusText(12, y, label, color);
        drawMechStatusText(90, y, std::to_wstring(working) + L" OF " + std::to_wstring(total), color);
    }

    void drawDamageArmorLine(const OwnedMech& mech, int y) {
        const uint8_t color = damageLevelArmorColor(mech);
        drawMechStatusText(12, y, L"ARMOR:", color);
        drawMechStatusText(90, y, std::to_wstring(mech.armorPercent) + L" %", color);
    }

    void drawDamageWeaponTable(const OwnedMech& mech) {
        static constexpr int kWeaponColumnX = 12;
        static constexpr int kLocationColumnX = 54;
        static constexpr int kConditionColumnX = 90;

        drawMechStatusText(kWeaponColumnX, kMechRepairWeaponHeaderY, L"WPN", 7);
        drawMechStatusText(kLocationColumnX, kMechRepairWeaponHeaderY, L"LOC", 7);
        drawMechStatusText(kConditionColumnX, kMechRepairWeaponHeaderY, L"CONDITION", 7);

        int y = kMechRepairWeaponFirstRowY;
        for (const MechWeaponStatus& weapon : mech.weapons) {
            if (weapon.weapon.empty()) {
                continue;
            }
            const uint8_t color = damageLevelConditionColor(weapon.condition);
            drawMechStatusText(kWeaponColumnX, y, weapon.weapon, color);
            drawMechStatusText(kLocationColumnX, y, weapon.location, color);
            drawMechStatusText(kConditionColumnX, y, damageLabel(weapon.condition), color);
            y += kMechRepairComponentLineStep;
        }
    }

    void drawStatusInfoPanel(int x, int y, int width) {
        drawTextPassCenteredInRect(smallFont_, x, y + 6, width, L"BLAZING ACES", 12);
        drawTextPass(smallFont_, x + 8, y + 16, L"COMMANDER:", 7);
        drawTextPass(smallFont_, x + 72, y + 16, commanderName_, 12);
        drawTextPass(smallFont_, x + 8, y + 24, L"AGE:", 7);
        drawTextPass(smallFont_, x + 38, y + 24, L" " + std::to_wstring(commanderAge()), 7);
        drawTextPass(smallFont_, x + 8, y + 32, L"REPUTATION:", 7);
        drawTextPass(smallFont_, x + 78, y + 32, reputationLabel(), 7);
        drawTextPass(smallFont_, x + 8, y + 40, L"WEALTH:", 7);
        drawTextPass(smallFont_, x + 56, y + 40, formatWealth(playerWealth_), 7);
        drawTextPass(smallFont_, x + 8, y + 48, L"FAMILY ATTITUDES:", 7);

        int lineY = y + 57;
        for (size_t i = 0; i < kHouseNames.size(); ++i) {
            drawTextPass(smallFont_, x + 16, lineY, kHouseNames[i], 7);
            drawTextPass(smallFont_, x + 64, lineY, attitudeLabel(familyAttitudes_[i]), 7);
            lineY += 8;
        }
    }

    void drawStatusActionPanel(int x, int y, int width) {
        drawMenuTextCenteredInRect(x, y + 11, width, L"BLAZING ACES", 12);
        constexpr int kStatusMenuLineStep = 10;
        for (size_t i = 0; i < kStatusMenuItems.size(); ++i) {
            const int itemY = y + 23 + static_cast<int>(i) * kStatusMenuLineStep;
            drawMenuTextCenteredInRect(
                x,
                itemY,
                width,
                kStatusMenuItems[i].label,
                i == statusMenuIndex_ ? 14 : 7);
        }
    }

    void drawSystemMenu(int x, int y, int width) {
        constexpr int kSystemMenuLineStep = 10;
        for (size_t i = 0; i < kSystemMenuItems.size(); ++i) {
            std::wstring label(kSystemMenuItems[i].label);
            if (kSystemMenuItems[i].action == MenuAction::ToggleSound) {
                label = soundEnabled_ ? L"TURN SOUND OFF" : L"TURN SOUND ON";
            } else if (kSystemMenuItems[i].action == MenuAction::Detail) {
                label = L"DETAIL: ";
                label += detailLabel();
            }

            const int itemY = y + static_cast<int>(i) * kSystemMenuLineStep;
            drawMenuTextCenteredInRect(x, itemY, width, label, i == systemMenuIndex_ ? 14 : 7);
        }
    }

    std::wstring_view detailLabel() const {
        switch (detailLevel_) {
        case 0:
            return L"LOW";
        case 1:
            return L"MED";
        default:
            return L"HIGH";
        }
    }

    int commanderAge() const {
        int age = currentYear_ - kCommanderBirthYear;
        const int displayedMonth = currentMonth_ + 1;
        if (displayedMonth < kCommanderBirthMonth ||
            (displayedMonth == kCommanderBirthMonth && currentDay_ < kCommanderBirthDay)) {
            --age;
        }
        return std::max(0, age);
    }

    std::wstring_view reputationLabel() const {
        switch (playerReputationTier_) {
        case 0:
            return L"RISKY";
        case 1:
            return L"WORTH WATCHING";
        case 2:
            return L"VETERAN";
        default:
            return L"ELITE";
        }
    }

    std::wstring_view attitudeLabel(int score) const {
        if (score < 0) {
            return L"NEGATIVE";
        }
        if (score == 0) {
            return L"NEUTRAL";
        }
        if (score < 10) {
            return L"POSITIVE";
        }
        return L"CONFIDENT";
    }

    std::wstring formatWealth(uint64_t value) const {
        value = std::min(value, kMaxPlayerWealth);
        const std::wstring digits = std::to_wstring(value);
        std::wstring result;
        result.reserve(digits.size() + digits.size() / 3);
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i > 0 && (digits.size() - i) % 3 == 0) {
                result.push_back(L',');
            }
            result.push_back(digits[i]);
        }
        return result;
    }

    static uint32_t integerSqrt(uint32_t value) {
        uint32_t low = 0;
        uint32_t high = 65535;
        while (low <= high) {
            const uint32_t mid = low + (high - low) / 2;
            const uint64_t square = static_cast<uint64_t>(mid) * static_cast<uint64_t>(mid);
            if (square == value) {
                return mid;
            }
            if (square < value) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return high;
    }

    uint16_t travelJumpCount(const PlanetRecord& from, const PlanetRecord& to) const {
        if (from.tableOrder == to.tableOrder) {
            return 0;
        }

        const int dx = std::abs(static_cast<int>(from.mapX) - static_cast<int>(to.mapX));
        const int dy = std::abs(static_cast<int>(from.mapY) - static_cast<int>(to.mapY));
        const int scaledDy = (dy * 32) / 20;
        const uint32_t distanceSquared = static_cast<uint32_t>(dx * dx + scaledDy * scaledDy);
        const uint32_t distance = integerSqrt(distanceSquared);
        return static_cast<uint16_t>(std::max<uint32_t>(1, distance / 18));
    }

    uint16_t travelJumpCount(int fromIndex, int toIndex) const {
        if (planets_.empty()) {
            return 0;
        }
        const int clampedFrom = std::clamp(fromIndex, 0, static_cast<int>(planets_.size()) - 1);
        const int clampedTo = std::clamp(toIndex, 0, static_cast<int>(planets_.size()) - 1);
        return travelJumpCount(planets_[clampedFrom], planets_[clampedTo]);
    }

    uint64_t travelCostForJumps(uint16_t jumpCount) const {
        if (jumpCount == 0) {
            return 0;
        }

        const uint64_t mechCount = std::min<uint64_t>(ownedMechs_.size(), kTravelMaxMechs);
        if (mechCount > 0) {
            return mechCount * (kTravelMechBaseCost + kTravelMechCostPerJump * jumpCount);
        }

        const uint64_t pilotCount = std::min<uint64_t>(hiredCrewCount(), kTravelMaxPilots);
        return pilotCount * kTravelPilotCostPerJump * jumpCount;
    }

    uint8_t hiredCrewCount() const {
        return static_cast<uint8_t>(std::count_if(
            crewMembers_.begin(),
            crewMembers_.end(),
            [](const CrewMember& member) { return member.hired; }));
    }

    uint64_t monthlyCrewPayroll() const {
        uint64_t total = 0;
        for (size_t i = 1; i < crewMembers_.size(); ++i) {
            if (crewMembers_[i].hired) {
                total += crewMembers_[i].wage;
            }
        }
        return total;
    }

    void payMonthlyCrewWages() {
        const uint64_t payroll = monthlyCrewPayroll();
        if (payroll == 0) {
            return;
        }
        playerWealth_ = (playerWealth_ > payroll) ? (playerWealth_ - payroll) : 0;
    }

    uint64_t travelCostBetween(int fromIndex, int toIndex) const {
        return travelCostForJumps(travelJumpCount(fromIndex, toIndex));
    }

    uint16_t travelDays(const PlanetRecord& from, const PlanetRecord& to) const {
        const uint16_t jumpCount = travelJumpCount(from, to);
        if (jumpCount == 0) {
            return 0;
        }
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(jumpCount) - 1u) * kPeriodicCampaignUpdateDays +
            static_cast<uint32_t>(from.unknownByte7) * 2u +
            static_cast<uint32_t>(to.unknownByte7) * 2u);
    }

    uint16_t travelDaysBetween(int fromIndex, int toIndex) const {
        if (planets_.empty()) {
            return 0;
        }
        const int clampedFrom = std::clamp(fromIndex, 0, static_cast<int>(planets_.size()) - 1);
        const int clampedTo = std::clamp(toIndex, 0, static_cast<int>(planets_.size()) - 1);
        return travelDays(planets_[clampedFrom], planets_[clampedTo]);
    }

    void advanceCampaignDays(uint32_t days) {
        const int elapsedMonthCount =
            (currentMonthDayCounter_ + static_cast<int>(days)) / kCampaignDaysPerMonth;
        for (int i = 0; i < elapsedMonthCount; ++i) {
            payMonthlyCrewWages();
        }

        currentMonthDayCounter_ += static_cast<int>(days);
        if (elapsedMonthCount > 0) {
            currentMonth_ += elapsedMonthCount;
            currentMonthDayCounter_ %= kCampaignDaysPerMonth;
        }

        if (currentMonth_ >= kCampaignMonthsPerYear) {
            currentYear_ += currentMonth_ / kCampaignMonthsPerYear;
            currentMonth_ %= kCampaignMonthsPerYear;
        }

        currentPeriodic14DayCounter_ += static_cast<int>(days);
        if (currentPeriodic14DayCounter_ >= kPeriodicCampaignUpdateDays) {
            currentPeriodic14DayCounter_ %= kPeriodicCampaignUpdateDays;
            // Original game runs periodic campaign updates here; those systems are not modeled yet.
        }
    }

    bool isNewsNetEntryAvailable(const NewsNetEntry& entry) const {
        const int currentMonthOneBased = currentMonth_ + 1;
        const int requiredDayCounter = std::max(0, (entry.day - 1) * 2);
        if (currentYear_ != entry.year) {
            return currentYear_ > entry.year;
        }
        if (currentMonthOneBased != entry.month) {
            return currentMonthOneBased > entry.month;
        }
        return currentMonthDayCounter_ >= requiredDayCounter;
    }

    bool isNewsNetMessageAvailable(size_t messageIndex) const {
        if (messageIndex < kNewsNetEntries.size()) {
            return isNewsNetEntryAvailable(kNewsNetEntries[messageIndex]);
        }
        const size_t storyIndex = messageIndex - kNewsNetEntries.size();
        if (storyIndex >= kStoryNewsNetEntries.size()) {
            return false;
        }
        const StoryNewsNetEntry& entry = kStoryNewsNetEntries[storyIndex];
        if (entry.messageId == 0x40 && storyFlag(0x39)) {
            return true;
        }
        return storyFlag(entry.prerequisiteMessageId);
    }

    uint8_t newsNetMessageId(size_t messageIndex) const {
        if (messageIndex < kNewsNetEntries.size()) {
            return kNewsNetEntries[messageIndex].messageId;
        }
        const size_t storyIndex = messageIndex - kNewsNetEntries.size();
        if (storyIndex < kStoryNewsNetEntries.size()) {
            return kStoryNewsNetEntries[storyIndex].messageId;
        }
        return 0;
    }

    void rebuildNewsNetMessages() {
        activeNewsNetMessageIndexes_.clear();
        for (size_t i = 0; i < newsNetMessageLines_.size(); ++i) {
            if (isNewsNetMessageAvailable(i)) {
                const uint8_t messageId = newsNetMessageId(i);
                if (messageId != 0) {
                    setStoryFlag(messageId);
                }
                activeNewsNetMessageIndexes_.push_back(i);
            }
        }

        constexpr size_t kVisibleNewsNetMessageCount = 7;
        if (activeNewsNetMessageIndexes_.size() > kVisibleNewsNetMessageCount) {
            activeNewsNetMessageIndexes_.erase(
                activeNewsNetMessageIndexes_.begin(),
                activeNewsNetMessageIndexes_.end() -
                    static_cast<std::ptrdiff_t>(kVisibleNewsNetMessageCount));
        }
    }

    std::wstring_view noOtherNewsNetMessageLine() const {
        if (!newsNetNoOtherLines_.empty()) {
            return newsNetNoOtherLines_.front();
        }
        return L"NO OTHER MESSAGES ON FILE!";
    }

    std::wstring currentPlanetName() const {
        if (planets_.empty()) {
            return widen(kFallbackStartingPlanetName);
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return widen(planets_[index].name);
    }

    uint8_t currentPlanetHouseId() const {
        if (planets_.empty()) {
            return 0;
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return static_cast<uint8_t>(std::min<uint8_t>(planets_[index].houseId, 4));
    }

    bool currentPlanetContractsAvailable() const {
        if (planets_.empty()) {
            return true;
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return planets_[index].contractAvailableFlag != 0;
    }

    bool planetIconAvailable(size_t iconIndex) const {
        if (contractAccepted_) {
            return iconIndex <= kPlanetContractIconIndex;
        }
        if (iconIndex == kPlanetContractIconIndex) {
            return currentPlanetContractsAvailable();
        }
        return iconIndex < kPlanetIconRects.size();
    }

    size_t nextAvailablePlanetIcon(size_t iconIndex, int delta) const {
        size_t candidate = iconIndex;
        for (size_t i = 0; i < kPlanetIconRects.size(); ++i) {
            candidate = delta < 0
                ? (candidate + kPlanetIconRects.size() - 1u) % kPlanetIconRects.size()
                : (candidate + 1u) % kPlanetIconRects.size();
            if (planetIconAvailable(candidate)) {
                return candidate;
            }
        }
        return kPlanetStatusIconIndex;
    }

    const ContractOffer* activeContractOffer() const {
        if (activeContractIndex_ >= activeContracts_.size()) {
            return nullptr;
        }
        return &activeContracts_[activeContractIndex_];
    }

    ContractOffer* activeContractOffer() {
        if (activeContractIndex_ >= activeContracts_.size()) {
            return nullptr;
        }
        return &activeContracts_[activeContractIndex_];
    }

    static std::wstring_view houseNamePlain(uint8_t houseId) {
        static constexpr std::array<std::wstring_view, 5> kPlainHouseNames = {{
            L"KURITA",
            L"STEINER",
            L"MARIK",
            L"LIAO",
            L"DAVION",
        }};
        return kPlainHouseNames[std::min<size_t>(houseId, kPlainHouseNames.size() - 1u)];
    }

    int contractForceScore() const {
        int tons = 0;
        int assignedMechs = 0;
        for (const OwnedMech& mech : ownedMechs_) {
            if (mech.assignedCrewSlot >= 0) {
                tons += mech.tons;
                ++assignedMechs;
            }
        }
        if (assignedMechs == 0) {
            for (const OwnedMech& mech : ownedMechs_) {
                tons += mech.tons;
            }
        }

        const int yearProgress = std::max(0, currentYear_ - kStartingYear);
        return std::max(1, tons / 50 + yearProgress * 2 + static_cast<int>(playerReputationTier_) / 2);
    }

    static bool contractMissionHasHostileTargetHouse(const ContractMissionDefinition& mission) {
        return mission.name != L"GARRISON DUTY" && mission.name != L"GENERAL SECURITY DUTY";
    }

    static void assignContractEnemyCounts(int score, ContractOffer& offer, uint32_t randomValue) {
        offer.heavyCount = score / 25;
        int remainder = score % 25;
        offer.mediumCount = remainder / 15;
        offer.lightCount = (remainder % 15) / 7;

        int total = offer.heavyCount + offer.mediumCount + offer.lightCount;
        if (total <= 0) {
            offer.lightCount = 1;
            total = 1;
        }
        while (total > 4) {
            if (offer.heavyCount > 0) {
                --offer.heavyCount;
            } else if (offer.mediumCount > 0) {
                --offer.mediumCount;
            } else {
                --offer.lightCount;
            }
            --total;
        }

        if (offer.heavyCount == 4) {
            if ((randomValue & 1u) == 0u) {
                offer.heavyCount = 3;
                offer.mediumCount = 1;
            } else {
                offer.heavyCount = 2;
                offer.mediumCount = 2;
            }
        }
    }

    static int contractBasePriceK(int score, uint8_t employerHouse, uint32_t randomValue) {
        static constexpr std::array<int, 5> kHousePriceBias = {{3, 5, 2, 4, 3}};
        static constexpr std::array<int, 5> kHousePriceMultiplier = {{7, 9, 6, 8, 7}};
        const int house = std::min<int>(employerHouse, 4);
        const int basePrice = ((std::max(1, score) >> 1) + 1) * 100;
        const int randomTerm = static_cast<int>(randomValue % 12u) + 1 + kHousePriceBias[house];
        const int price = basePrice + (randomTerm * basePrice * kHousePriceMultiplier[house]) / 100;
        return std::clamp(roundToNearest10(price), 100, kContractMaxPriceK);
    }

    static int contractDefaultSalvagePercent(uint8_t employerHouse, uint32_t randomValue) {
        static constexpr std::array<int, 5> kHouseSalvage = {{4, 5, 5, 4, 5}};
        const int house = std::min<int>(employerHouse, 4);
        return std::clamp(kHouseSalvage[house] + static_cast<int>(randomValue % 2u), 0, 100);
    }

    static int contractDefaultAdvancePercent(uint8_t employerHouse, uint32_t randomValue) {
        static constexpr std::array<int, 5> kHouseAdvance = {{4, 5, 3, 3, 5}};
        const int house = std::min<int>(employerHouse, 4);
        return std::clamp(kHouseAdvance[house] + static_cast<int>(randomValue % 2u), 0, 100);
    }

    static int roundToNearest10(int value) {
        return ((value + 5) / 10) * 10;
    }

    const ContractMissionDefinition& chooseContractMission(bool allowExtended, std::mt19937& rng) const {
        for (int attempts = 0; attempts < 12; ++attempts) {
            const size_t index = static_cast<size_t>(rng() % kContractMissionDefinitions.size());
            const ContractMissionDefinition& mission = kContractMissionDefinitions[index];
            if (allowExtended || !mission.extended) {
                return mission;
            }
        }
        return kContractMissionDefinitions[static_cast<size_t>(rng() % 14u)];
    }

    std::wstring contractTargetPlanetName(uint8_t targetHouse, std::mt19937& rng) const {
        std::vector<size_t> candidates;
        for (size_t i = 0; i < planets_.size(); ++i) {
            if (planets_[i].houseId == targetHouse) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) {
            return currentPlanetName();
        }
        const size_t planetIndex = candidates[static_cast<size_t>(rng() % candidates.size())];
        return widen(planets_[planetIndex].name);
    }

    void beginContractTermEdit(ContractEditableField field) {
        ContractOffer* offer = activeContractOffer();
        if (!offer) {
            return;
        }
        contractEditableField_ = field;
        offer->termsModified = true;
        contractNegotiationButtonIndex_ = 0;
    }

    void adjustSelectedContractTerm(int direction) {
        ContractOffer* offer = activeContractOffer();
        if (!offer) {
            return;
        }

        if (contractEditableField_ == ContractEditableField::Price) {
            offer->priceK = std::clamp(
                offer->priceK + direction * kContractPriceStepK,
                0,
                kContractMaxPriceK);
        } else if (contractEditableField_ == ContractEditableField::Salvage) {
            offer->salvagePercent = std::clamp(offer->salvagePercent + direction, 0, 100);
        } else if (contractEditableField_ == ContractEditableField::Advance) {
            offer->advancePercent = std::clamp(offer->advancePercent + direction, 0, 100);
        }
        offer->termsModified = true;
        contractNegotiationButtonIndex_ = 0;
    }

    void submitContractCounterOffer() {
        ContractOffer* offer = activeContractOffer();
        if (!offer) {
            return;
        }

        ++offer->negotiationRounds;
        if (contractOfferIsUnacceptable(*offer)) {
            terminateContractNegotiations();
            return;
        }

        offer->priceK = contractCounterPriceK(*offer);
        offer->salvagePercent = contractCounterPercent(
            offer->houseSalvagePercent,
            offer->salvagePercent,
            offer->negotiationRounds,
            1);
        offer->advancePercent = contractCounterPercent(
            offer->houseAdvancePercent,
            offer->advancePercent,
            offer->negotiationRounds,
            1);
        offer->termsModified = false;
        contractEditableField_ = ContractEditableField::None;
        contractNegotiationButtonIndex_ = 0;
    }

    void acceptActiveContract() {
        const ContractOffer* offer = activeContractOffer();
        if (!offer) {
            return;
        }
        acceptedContract_ = *offer;
        contractAccepted_ = true;
        activeContracts_.clear();
        activeContractIndex_ = 0;
        contractEditableField_ = ContractEditableField::None;
        planetMenuIndex_ = kPlanetContractIconIndex;
        changeState(ScreenState::ContractAcceptedMessage);
    }

    void beginMissionLaunch() {
        if (!contractAccepted_) {
            return;
        }
        missionParticipants_ = currentMissionParticipants();
        if (missionParticipants_.empty()) {
            return;
        }
        finalMissionStubActive_ = false;
        missionLaunchPending_ = true;
        changeState(ScreenState::TravelAnimation);
    }

    void handleBattleStubClick(int screenX, int screenY) {
        if (hitRect(kBattleStubWinButtonRect, screenX, screenY)) {
            battleStubButtonIndex_ = 0;
            activateBattleStubSelection();
        } else if (hitRect(kBattleStubLoseButtonRect, screenX, screenY)) {
            battleStubButtonIndex_ = 1;
            activateBattleStubSelection();
        } else if (hitRect(kBattleStubRunButtonRect, screenX, screenY)) {
            battleStubButtonIndex_ = 2;
            activateBattleStubSelection();
        }
    }

    void activateBattleStubSelection() {
        if (finalMissionStubActive_) {
            finalMissionStubActive_ = false;
            planetMenuIndex_ = kPlanetStatusIconIndex;
            changeState(ScreenState::MainMenu);
            return;
        }

        if (battleStubButtonIndex_ == 0) {
            resolveMissionOutcome(MissionOutcome::Victory);
        } else if (battleStubButtonIndex_ == 1) {
            resolveMissionOutcome(MissionOutcome::Defeat);
        } else {
            resolveMissionOutcome(MissionOutcome::Death);
        }
    }

    void handleMissionDebriefClick(int screenX, int screenY) {
        if (missionDebriefOutcome_ != MissionOutcome::Death) {
            changeState(ScreenState::MainMenu);
            return;
        }
        if (hitRect(kDeathPlayAgainRect, screenX, screenY)) {
            missionDeathMenuIndex_ = 0;
            restartCampaign();
        } else if (hitRect(kDeathQuitRect, screenX, screenY)) {
            missionDeathMenuIndex_ = 1;
            DestroyWindow(hwnd_);
        }
    }

    std::vector<MissionParticipant> currentMissionParticipants() const {
        std::vector<MissionParticipant> participants;
        participants.reserve(crewMembers_.size());
        for (size_t mechIndex = 0; mechIndex < ownedMechs_.size(); ++mechIndex) {
            const OwnedMech& mech = ownedMechs_[mechIndex];
            if (mech.assignedCrewSlot < 0 ||
                static_cast<size_t>(mech.assignedCrewSlot) >= crewMembers_.size()) {
                continue;
            }
            const CrewMember& crew = crewMembers_[static_cast<size_t>(mech.assignedCrewSlot)];
            if (!crew.hired) {
                continue;
            }
            participants.push_back({
                mech.assignedCrewSlot,
                static_cast<int>(mechIndex),
                std::wstring(crew.name),
                std::wstring(mech.name),
                mech.armorPercent,
                false,
            });
            if (participants.size() >= crewMembers_.size()) {
                break;
            }
        }
        std::sort(
            participants.begin(),
            participants.end(),
            [](const MissionParticipant& lhs, const MissionParticipant& rhs) {
                return lhs.crewSlot < rhs.crewSlot;
            });
        return participants;
    }

    void resolveMissionOutcome(MissionOutcome outcome) {
        if (!contractAccepted_ && outcome != MissionOutcome::Death) {
            return;
        }
        if (missionParticipants_.empty()) {
            missionParticipants_ = currentMissionParticipants();
        }
        if (outcome == MissionOutcome::Death) {
            for (MissionParticipant& participant : missionParticipants_) {
                participant.killed = participant.crewSlot == 0;
            }
        }

        missionDebriefOutcome_ = outcome;
        missionDebriefSalvage_ = outcome == MissionOutcome::Victory ? missionPlaceholderSalvage() : 0;
        missionDebriefPayment_ = outcome == MissionOutcome::Victory
            ? std::min<uint64_t>(
                  kMaxPlayerWealth,
                  static_cast<uint64_t>(acceptedContract_.priceK) * 1000ull)
            : 0;

        if (outcome == MissionOutcome::Victory) {
            playerWealth_ = std::min(kMaxPlayerWealth, playerWealth_ + missionDebriefPayment_ + missionDebriefSalvage_);
            applyMissionHouseConsequences(outcome);
            addCompanyReputationPoints(missionReputationDelta(acceptedContract_));
            applyMissionExperience();
        } else if (outcome == MissionOutcome::Defeat) {
            applyMissionHouseConsequences(outcome);
            applyMissionExperience();
        }

        if (outcome != MissionOutcome::Death) {
            advanceCampaignDays(missionDurationTicks(acceptedContract_));
        }

        contractAccepted_ = false;
        missionLaunchPending_ = false;
        activeContracts_.clear();
        planetMenuIndex_ = kPlanetStatusIconIndex;
        missionDeathMenuIndex_ = 0;
        changeState(ScreenState::MissionDebrief);
    }

    uint64_t missionPlaceholderSalvage() const {
        const uint64_t enemyPool =
            static_cast<uint64_t>(acceptedContract_.heavyCount) * 800000ull +
            static_cast<uint64_t>(acceptedContract_.mediumCount) * 450000ull +
            static_cast<uint64_t>(acceptedContract_.lightCount) * 180000ull;
        return (enemyPool * static_cast<uint64_t>(acceptedContract_.salvagePercent)) / 100ull;
    }

    void applyMissionHouseConsequences(MissionOutcome outcome) {
        if (acceptedContract_.hasHostileTargetHouse) {
            addHouseNegativeCounter(acceptedContract_.targetHouse, 2);
        }

        if (outcome == MissionOutcome::Victory) {
            addHousePositiveCounter(acceptedContract_.employerHouse, 2);
        } else if (outcome == MissionOutcome::Defeat) {
            addHouseNegativeCounter(acceptedContract_.employerHouse, 1);
        }
        syncFamilyAttitudesFromHouseCounters();
    }

    void addHousePositiveCounter(uint8_t houseId, int delta) {
        const size_t index = std::min<size_t>(houseId, housePositiveCounters_.size() - 1u);
        housePositiveCounters_[index] = std::max(0, housePositiveCounters_[index] + delta);
    }

    void addHouseNegativeCounter(uint8_t houseId, int delta) {
        const size_t index = std::min<size_t>(houseId, houseNegativeCounters_.size() - 1u);
        houseNegativeCounters_[index] = std::max(0, houseNegativeCounters_[index] + delta);
    }

    void syncFamilyAttitudesFromHouseCounters() {
        for (size_t i = 0; i < familyAttitudes_.size(); ++i) {
            familyAttitudes_[i] = housePositiveCounters_[i] - houseNegativeCounters_[i];
        }
    }

    static int missionReputationDelta(const ContractOffer& offer) {
        return std::max(0, offer.heavyCount + offer.mediumCount + offer.lightCount);
    }

    static uint32_t missionDurationTicks(const ContractOffer& offer) {
        const uint32_t enemyWeight =
            static_cast<uint32_t>(std::max(0, offer.heavyCount)) * 24u +
            static_cast<uint32_t>(std::max(0, offer.mediumCount)) * 18u +
            static_cast<uint32_t>(std::max(0, offer.lightCount)) * 12u;
        uint32_t seed = 0x4D575243u;
        seed ^= static_cast<uint32_t>(offer.employerHouse) * 0x9E3779B9u;
        seed ^= static_cast<uint32_t>(offer.targetHouse) * 0x85EBCA6Bu;
        seed ^= static_cast<uint32_t>(offer.priceK) * 0xC2B2AE35u;
        seed ^= static_cast<uint32_t>(offer.salvagePercent) * 0x27D4EB2Du;
        seed ^= static_cast<uint32_t>(offer.advancePercent) * 0x165667B1u;
        seed ^= enemyWeight * 0xD3A2646Cu;

        if (offer.hasHostileTargetHouse) {
            return kMissionHostileBaseDuration + enemyWeight + seed % 36u;
        }
        return kMissionGarrisonBaseDuration + enemyWeight + seed % 72u;
    }

    void addCompanyReputationPoints(int delta) {
        playerReputationPoints_ = static_cast<uint16_t>(
            std::min<int>(
                std::numeric_limits<uint16_t>::max(),
                static_cast<int>(playerReputationPoints_) + std::max(0, delta)));
        playerReputationTier_ = reputationTierForPoints(playerReputationPoints_);
    }

    static uint8_t reputationTierForPoints(uint16_t points) {
        if (points > 20) {
            return 3;
        }
        if (points > 10) {
            return 2;
        }
        if (points > 5) {
            return 1;
        }
        return 0;
    }

    static uint16_t minimumReputationPointsForTier(uint8_t tier) {
        switch (std::min<uint8_t>(tier, 3)) {
        case 1:
            return 6;
        case 2:
            return 11;
        case 3:
            return 21;
        default:
            return 0;
        }
    }

    void applyMissionExperience() {
        for (const MissionParticipant& participant : missionParticipants_) {
            if (participant.killed ||
                participant.crewSlot < 0 ||
                static_cast<size_t>(participant.crewSlot) >= crewMembers_.size()) {
                continue;
            }
            improveCrewMemberAfterMission(crewMembers_[static_cast<size_t>(participant.crewSlot)]);
        }
    }

    void improveCrewMemberAfterMission(CrewMember& member) {
        const uint8_t skill = skillRank(member.gunnery);
        if (skill >= 3) {
            return;
        }
        static constexpr std::array<uint8_t, 4> kPromotionMissionThresholds = {{3, 10, 15, 255}};
        ++member.missionExperience;
        if (member.missionExperience < kPromotionMissionThresholds[skill]) {
            return;
        }

        const uint8_t promotedSkill = static_cast<uint8_t>(std::min<int>(3, skill + 1));
        member.gunnery = skillLabel(promotedSkill);
        member.piloting = skillLabel(promotedSkill);
        member.missionExperience = 0;
        if (member.recruitIndex >= 0 && static_cast<size_t>(member.recruitIndex) < recruitPilots_.size()) {
            RecruitPilot& pilot = recruitPilots_[static_cast<size_t>(member.recruitIndex)];
            pilot.gunnerySkill = promotedSkill;
            pilot.pilotingSkill = promotedSkill;
            pilot.monthlyWage = monthlyWageForGunnery(promotedSkill);
            member.wage = pilot.monthlyWage;
        } else if (member.wage > 0) {
            member.wage = monthlyWageForGunnery(promotedSkill);
        }
    }

    static uint8_t skillRank(std::wstring_view label) {
        if (label == L"AVERAGE") {
            return 1;
        }
        if (label == L"GOOD") {
            return 2;
        }
        if (label == L"EXCELLENT") {
            return 3;
        }
        return 0;
    }

    void restartCampaign() {
        loadResources();
        changeState(ScreenState::CampaignMessage);
    }

    bool contractOfferIsUnacceptable(const ContractOffer& offer) const {
        const int patience = contractHousePatience(offer.employerHouse);
        if (offer.negotiationRounds > patience) {
            return true;
        }

        const int priceDemand = std::max(0, offer.priceK - offer.housePriceK) / 10;
        const int salvageDemand = std::max(0, offer.salvagePercent - offer.houseSalvagePercent) * 5;
        const int advanceDemand = std::max(0, offer.advancePercent - offer.houseAdvancePercent) * 3;
        const int demandScore = priceDemand + salvageDemand + advanceDemand;
        return demandScore > 150 + patience * 20;
    }

    static int contractHousePatience(uint8_t employerHouse) {
        static constexpr std::array<int, 5> kHousePatience = {{3, 4, 3, 3, 4}};
        return kHousePatience[std::min<size_t>(employerHouse, kHousePatience.size() - 1u)];
    }

    static int contractCounterPriceK(const ContractOffer& offer) {
        const int requestedExtra = std::max(0, offer.priceK - offer.housePriceK);
        const int concession = std::min(requestedExtra, 20 * offer.negotiationRounds);
        if (offer.priceK <= offer.housePriceK) {
            return offer.priceK;
        }
        return std::clamp(roundToNearest10(offer.housePriceK + concession), 0, kContractMaxPriceK);
    }

    static int contractCounterPercent(int houseValue, int requestedValue, int negotiationRounds, int concessionPerRound) {
        if (requestedValue <= houseValue) {
            return requestedValue;
        }
        const int concession = std::min(requestedValue - houseValue, negotiationRounds * concessionPerRound);
        return std::clamp(houseValue + concession, 0, 100);
    }

    void terminateContractNegotiations() {
        if (!planets_.empty()) {
            const size_t planetIndex = static_cast<size_t>(
                std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1));
            if (planetIndex < contractNegotiationLockedVisitByPlanet_.size()) {
                contractNegotiationLockedVisitByPlanet_[planetIndex] = currentPlanetVisitSerial_;
            }
        }
        activeContracts_.clear();
        activeContractIndex_ = 0;
        contractEditableField_ = ContractEditableField::None;
        contractNegotiationButtonIndex_ = 2;
        contractNegotiationTerminated_ = true;
    }

    bool currentPlanetContractsLocked() const {
        if (planets_.empty()) {
            return false;
        }
        const size_t planetIndex = static_cast<size_t>(
            std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1));
        return planetIndex < contractNegotiationLockedVisitByPlanet_.size() &&
            contractNegotiationLockedVisitByPlanet_[planetIndex] == currentPlanetVisitSerial_;
    }

    void cycleContractEditableField() {
        if (contractEditableField_ == ContractEditableField::Price) {
            contractEditableField_ = ContractEditableField::Salvage;
        } else if (contractEditableField_ == ContractEditableField::Salvage) {
            contractEditableField_ = ContractEditableField::Advance;
        } else {
            contractEditableField_ = ContractEditableField::Price;
        }
    }

    int currentPlanetContractPortraitEntry() const {
        const size_t houseId = currentPlanetHouseId();
        const size_t portraitCount = kContractContactPortraitCounts[houseId];
        const size_t slot = portraitCount == 0 ? 0 : currentPlanetHouseOrdinal(static_cast<uint8_t>(houseId)) % portraitCount;
        return contractContactPortraitEntryIndex(houseId, slot);
    }

    size_t currentPlanetHouseOrdinal(uint8_t houseId) const {
        if (planets_.empty()) {
            return 0;
        }

        const int currentIndex = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        const PlanetRecord& currentPlanet = planets_[currentIndex];
        size_t ordinal = 0;
        for (const PlanetRecord& planet : planets_) {
            if (planet.houseId != houseId) {
                continue;
            }
            if (planet.tableOrder < currentPlanet.tableOrder ||
                (planet.tableOrder == currentPlanet.tableOrder && planet.planetNumber < currentPlanet.planetNumber)) {
                ++ordinal;
            }
        }
        return ordinal;
    }

    static int contractIconEntryForHouse(uint8_t houseId) {
        return 26 + static_cast<int>(std::min<uint8_t>(houseId, 4));
    }

    static int contractContactPortraitEntryIndex(size_t houseId, size_t slot) {
        return static_cast<int>(houseId * 5u + slot);
    }

    std::wstring campaignDateLabel() const {
        static constexpr std::array<std::wstring_view, 12> kMonthNames = {{
            L"JANUARY",
            L"FEBRUARY",
            L"MARCH",
            L"APRIL",
            L"MAY",
            L"JUNE",
            L"JULY",
            L"AUGUST",
            L"SEPTEMBER",
            L"OCTOBER",
            L"NOVEMBER",
            L"DECEMBER",
        }};
        const int month = std::clamp(currentMonth_, 0, static_cast<int>(kMonthNames.size()) - 1);
        return std::wstring(kMonthNames[month]) + L", " + std::to_wstring(currentYear_);
    }

    int currentMonthKey() const {
        return currentYear_ * kCampaignMonthsPerYear + currentMonth_;
    }

    static PlanetEnvironment environmentForTerrain(uint8_t terrainCode) {
        switch (terrainCode % 3u) {
        case 1:
            return PlanetEnvironment::Desert;
        case 2:
            return PlanetEnvironment::Tropical;
        default:
            return PlanetEnvironment::Ice;
        }
    }

    PlanetEnvironment currentPlanetEnvironment() const {
        if (planets_.empty()) {
            return PlanetEnvironment::Desert;
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return environmentForTerrain(planets_[static_cast<size_t>(index)].terrainCode);
    }

    int campaignBackdropEntry() const {
        switch (currentPlanetEnvironment()) {
        case PlanetEnvironment::Tropical:
            return kCampaignTropicalEntry;
        case PlanetEnvironment::Ice:
            return kCampaignIceEntry;
        case PlanetEnvironment::Desert:
        default:
            return kCampaignDesertEntry;
        }
    }

    int barEnvironmentOverlayEntry() const {
        switch (currentPlanetEnvironment()) {
        case PlanetEnvironment::Tropical:
            return kBarTropicalOverlayEntry;
        case PlanetEnvironment::Ice:
            return kBarIceOverlayEntry;
        case PlanetEnvironment::Desert:
        default:
            return -1;
        }
    }

    std::wstring_view environmentLabel(uint8_t terrainCode) const {
        switch (environmentForTerrain(terrainCode)) {
        case PlanetEnvironment::Desert:
            return L"DESERT";
        case PlanetEnvironment::Tropical:
            return L"TROPICAL";
        case PlanetEnvironment::Ice:
        default:
            return L"ICE";
        }
    }

    int starmapTextWidth5x5(std::wstring_view text) const {
        return static_cast<int>(text.size()) * 6;
    }

    void drawCrewGridLines(uint8_t color) {
        static constexpr std::array<int, 5> kVerticalLines = {{44, 113, 182, 251, 319}};
        static constexpr std::array<int, 2> kHorizontalLines = {{68, 131}};

        for (int x : kVerticalLines) {
            fillRect(x, 0, 1, kScreenHeight, color);
        }
        for (int y : kHorizontalLines) {
            fillRect(0, y, kScreenWidth, 1, color);
        }
    }

    void drawCrewText5x5(
        int x,
        int y,
        std::wstring_view text,
        uint8_t color,
        int maxWidth) {
        if (smallFont_.rows.empty()) {
            return;
        }

        int cursorX = x;
        const int right = x + maxWidth;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX + 5 > right) {
                break;
            }
            drawGlyph5x5(smallFont_, cursorX, y, ch, color);
            cursorX += 6;
        }
    }

    int recruitTextWidth7x5(std::wstring_view text) const {
        return static_cast<int>(text.size()) * 8;
    }

    void drawRecruitText7x5(
        int x,
        int y,
        std::wstring_view text,
        uint8_t color,
        int maxWidth) {
        const Font& recruitFont = menuFont_.rows.empty() ? font_ : menuFont_;
        if (recruitFont.rows.empty()) {
            return;
        }

        int cursorX = x;
        const int right = x + maxWidth;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX + 7 > right) {
                break;
            }
            drawGlyph7x5(recruitFont, cursorX, y, ch, color);
            cursorX += 8;
        }
    }

    void drawRecruitTextCenteredInRect(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        drawRecruitText7x5(x + (width - recruitTextWidth7x5(text)) / 2, y, text, color, width);
    }

    void drawStarmapText5x5(
        int x,
        int y,
        std::wstring_view text,
        uint8_t color,
        int maxWidth = kScreenWidth) {
        if (smallFont_.rows.empty()) {
            return;
        }

        int cursorX = x;
        const int right = x + maxWidth;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX + 5 > right) {
                break;
            }
            drawGlyph5x5(smallFont_, cursorX, y, ch, color);
            cursorX += 6;
        }
    }

    void drawStarmapText5x5Centered(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = starmapTextWidth5x5(text);
        drawStarmapText5x5(x + (width - measuredWidth) / 2, y, text, color, width);
    }

    int newsNetTextWidth5x5(std::wstring_view text) const {
        return static_cast<int>(text.size()) * 6;
    }

    int newsNetButtonTextWidth(std::wstring_view text) const {
        return static_cast<int>(text.size()) * 8;
    }

    void drawNewsNetText5x5(
        int x,
        int y,
        std::wstring_view text,
        uint8_t color,
        int maxWidth) {
        if (smallFont_.rows.empty()) {
            return;
        }

        int cursorX = x;
        const int right = x + maxWidth;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX + 5 > right) {
                break;
            }
            drawGlyph5x5(smallFont_, cursorX, y, ch, color);
            cursorX += 6;
        }
    }

    void drawNewsNetText5x5Centered(
        int x,
        int y,
        int width,
        std::wstring_view text,
        uint8_t color) {
        const int measuredWidth = newsNetTextWidth5x5(text);
        drawNewsNetText5x5(x + (width - measuredWidth) / 2, y, text, color, width);
    }

    void drawNewsNetMessage(const std::vector<std::wstring>& lines) {
        constexpr int kTextX = 31;
        constexpr int kTextY = 25;
        constexpr int kTextWidth = 258;
        constexpr int kLineStep = 7;
        constexpr int kTextBottom = 176;

        int y = kTextY;
        for (const std::wstring& line : lines) {
            if (y + 5 > kTextBottom) {
                break;
            }
            drawNewsNetText5x5(kTextX, y, line, kNewsNetTextColor, kTextWidth);
            y += kLineStep;
        }
    }

    void drawNewsNetButton(const RectI& rect, std::wstring_view label, bool selected, int textXOffset = 0) {
        const Font& buttonFont = menuFont_.rows.empty() ? font_ : menuFont_;
        const int textX = rect.x + (rect.width - newsNetButtonTextWidth(label)) / 2 + textXOffset;
        const int textY = rect.y + (rect.height - 5) / 2;
        int cursorX = textX;
        for (wchar_t wideCh : label) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            drawGlyph7x5(buttonFont, cursorX, textY, ch, selected ? 14 : 7);
            cursorX += 8;
        }
    }

    void drawContractText7x5(int x, int y, std::wstring_view text, uint8_t color) {
        const Font& contractFont = menuFont_.rows.empty() ? font_ : menuFont_;
        int cursorX = x;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (cursorX > 286) {
                break;
            }
            drawGlyph7x5(contractFont, cursorX, y, ch, color);
            cursorX += 8;
        }
    }

    void drawContractText7x5Centered(int x, int y, int width, std::wstring_view text, uint8_t color) {
        const int measuredWidth = newsNetButtonTextWidth(text);
        drawContractText7x5(x + (width - measuredWidth) / 2, y, text, color);
    }

    void drawContractValue7x5(int x, int y, std::wstring_view text, ContractEditableField field) {
        const uint8_t color = contractEditableField_ == field
            ? kContractSelectedValueColor
            : kContractValueColor;
        drawContractText7x5(x, y, text, color);
    }

    static std::vector<std::wstring> wrapContractLine(std::wstring_view text, size_t maxChars) {
        std::vector<std::wstring> lines;
        std::wstring current;
        size_t index = 0;
        while (index < text.size()) {
            while (index < text.size() && text[index] == L' ') {
                ++index;
            }
            size_t end = index;
            while (end < text.size() && text[end] != L' ') {
                ++end;
            }
            std::wstring word(text.substr(index, end - index));
            if (!current.empty() && current.size() + 1u + word.size() > maxChars) {
                lines.push_back(current);
                current.clear();
            }
            if (!current.empty()) {
                current.push_back(L' ');
            }
            current += word;
            index = end;
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        if (lines.empty()) {
            lines.push_back(std::wstring(text));
        }
        return lines;
    }

    void drawHouseEmblem(uint8_t houseId) {
        if (houseId > 4) {
            return;
        }
        const bool hasEmblem = std::any_of(
            houseEmblemsArchive_.images.begin(),
            houseEmblemsArchive_.images.end(),
            [houseId](const Image4bpp& image) { return image.entryIndex == static_cast<int>(houseId); });
        if (!hasEmblem) {
            return;
        }
        drawImageAt(houseEmblemsArchive_, static_cast<int>(houseId), 240, 34, false);
    }

    void drawStarmapMarker(uint8_t mapX, uint8_t mapY, uint8_t color) {
        drawBox(static_cast<int>(mapX) - 2, static_cast<int>(mapY) - 2, 5, 5, color);
    }

    void drawStarmapNameInputCursor(int x, int y) {
        if (((GetTickCount() - stateStartedTick_) / 300u) % 2u != 0u) {
            return;
        }

        drawPixel(x + 2, y, 4);
        fillRect(x + 1, y + 1, 3, 1, 4);
        fillRect(x, y + 2, 5, 1, 4);
        fillRect(x + 1, y + 3, 3, 1, 4);
        drawPixel(x + 2, y + 4, 4);
        drawPixel(x + 2, y + 2, 12);
    }

    void drawStarmapRouteLine(const PlanetRecord& from, const PlanetRecord& to) {
        int x0 = static_cast<int>(from.mapX);
        int y0 = static_cast<int>(from.mapY);
        const int x1 = static_cast<int>(to.mapX);
        const int y1 = static_cast<int>(to.mapY);
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;

        while (true) {
            drawPixel(x0, y0, 15);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * error;
            if (e2 >= dy) {
                error += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    void drawImageCentered(const Image4bpp& image) {
        drawImageCentered(image, archive_);
    }

    void drawImageCentered(const Image4bpp& image, const PicsArchive& archive) {
        const int startX = (kScreenWidth - image.width) / 2;
        const int startY = (kScreenHeight - image.height) / 2;
        drawImageAt(image, archive, startX, startY, false);
    }

    void drawImageAt(
        const Image4bpp& image,
        const PicsArchive& archive,
        int startX,
        int startY,
        bool transparentZero,
        const RectI* clip = nullptr) {
        for (int y = 0; y < image.height; ++y) {
            const int dstY = startY + y;
            if (dstY < 0 || dstY >= kScreenHeight) {
                continue;
            }
            for (int x = 0; x < image.width; ++x) {
                const int dstX = startX + x;
                if (dstX < 0 || dstX >= kScreenWidth) {
                    continue;
                }
                if (clip &&
                    (dstX < clip->x ||
                     dstX >= clip->x + clip->width ||
                     dstY < clip->y ||
                     dstY >= clip->y + clip->height)) {
                    continue;
                }
                const uint8_t byte = image.pixels[static_cast<size_t>(y) * image.rowStride + x / 2];
                const uint8_t colorIndex = (x % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
                if (transparentZero && colorIndex == 0) {
                    continue;
                }
                framebuffer_[static_cast<size_t>(dstY) * kScreenWidth + dstX] =
                    toBgra(imagePaletteColor(colorIndex, archive));
            }
        }
    }

    void resetMechLabAnimation(DWORD now) {
        mechLabWeldingActive_ = false;
        mechLabWeldStartedTick_ = now;
        scheduleNextMechLabWeld(now);
    }

    void scheduleNextMechLabWeld(DWORD now) {
        static constexpr std::array<DWORD, 4> delays = {{
            2200,
            3100,
            3900,
            2600,
        }};
        const DWORD delay = delays[mechLabWeldSequence_ % delays.size()];
        ++mechLabWeldSequence_;
        mechLabNextWeldTick_ = now + delay;
    }

    void updateMechLabAnimation(DWORD now) {
        if (mechLabWeldingActive_) {
            if (now - mechLabWeldStartedTick_ >= mechLabWeldDurationMs_) {
                mechLabWeldingActive_ = false;
                scheduleNextMechLabWeld(now);
            }
            return;
        }

        if (now - mechLabNextWeldTick_ < 0x80000000u) {
            mechLabWeldingActive_ = true;
            mechLabWeldStartedTick_ = now;
            mechLabWeldDurationMs_ =
                kMechLabWeldDurations[mechLabWeldSequence_ % kMechLabWeldDurations.size()];
        }
    }

    Color paletteColor(uint8_t colorIndex) const {
        const size_t index = colorIndex & 0x0F;
        if (archive_.palette.size() >= 16) {
            return archive_.palette[index];
        }
        return kEgaPalette[index];
    }

    Color imagePaletteColor(uint8_t colorIndex) const {
        return paletteColor(kPicsSourceToGamePaletteIndex[colorIndex & 0x0F]);
    }

    Color imagePaletteColor(uint8_t colorIndex, const PicsArchive& archive) const {
        const size_t mapped = kPicsSourceToGamePaletteIndex[colorIndex & 0x0F];
        if (archive.palette.size() >= 16) {
            return archive.palette[mapped];
        }
        return kEgaPalette[mapped];
    }

    void drawText(int x, int y, std::wstring_view text, uint8_t color, uint8_t shadow) {
        drawTextWithFont(font_, x, y, text, color, shadow);
    }

    void drawSmallText(int x, int y, std::wstring_view text, uint8_t color, uint8_t shadow) {
        drawTextWithFont(smallFont_, x, y, text, color, shadow);
    }

    void drawSmallTextPlain(int x, int y, std::wstring_view text, uint8_t color) {
        drawTextPass(smallFont_, x, y, text, color);
    }

    int textWidth(const Font& font, std::wstring_view text) const {
        return static_cast<int>(text.size()) * font.width;
    }

    void drawTextWithFont(const Font& font, int x, int y, std::wstring_view text, uint8_t color, uint8_t shadow) {
        if (font.rows.empty()) {
            return;
        }
        drawTextPass(font, x + 1, y + 1, text, shadow);
        drawTextPass(font, x, y, text, color);
    }

    void drawTextPass(const Font& font, int x, int y, std::wstring_view text, uint8_t color) {
        int cursorX = x;
        for (wchar_t wideCh : text) {
            const char ch = (wideCh < 128) ? static_cast<char>(wideCh) : '?';
            if (ch == '\n') {
                cursorX = x;
                y += font.height + 1;
                continue;
            }
            drawGlyph(font, cursorX, y, ch, color);
            cursorX += font.width;
            if (cursorX >= kScreenWidth - font.width) {
                break;
            }
        }
    }

    void drawGlyph(const Font& font, int x, int y, char ch, uint8_t color) {
        if (!font.contains(ch)) {
            return;
        }
        const int glyph = static_cast<unsigned char>(ch) - font.firstCode;
        const uint32_t bgra = toBgra(paletteColor(color));
        for (int row = 0; row < font.height; ++row) {
            const uint8_t bits = font.rows[static_cast<size_t>(glyph) * font.height + row];
            const int dstY = y + row;
            if (dstY < 0 || dstY >= kScreenHeight) {
                continue;
            }
            for (int col = 0; col < font.width; ++col) {
                if (((bits >> (7 - col)) & 1) == 0) {
                    continue;
                }
                const int dstX = x + col;
                if (dstX >= 0 && dstX < kScreenWidth) {
                    framebuffer_[static_cast<size_t>(dstY) * kScreenWidth + dstX] = bgra;
                }
            }
        }
    }

    void drawGlyph5x5(const Font& font, int x, int y, char ch, uint8_t color) {
        if (!font.contains(ch)) {
            return;
        }
        const int glyph = static_cast<unsigned char>(ch) - font.firstCode;
        const uint32_t bgra = toBgra(paletteColor(color));
        const int rows = std::min(5, font.height);
        const int cols = std::min(5, font.width);
        for (int row = 0; row < rows; ++row) {
            const uint8_t bits = font.rows[static_cast<size_t>(glyph) * font.height + row];
            const int dstY = y + row;
            if (dstY < 0 || dstY >= kScreenHeight) {
                continue;
            }
            for (int col = 0; col < cols; ++col) {
                if (((bits >> (7 - col)) & 1) == 0) {
                    continue;
                }
                const int dstX = x + col;
                if (dstX >= 0 && dstX < kScreenWidth) {
                    framebuffer_[static_cast<size_t>(dstY) * kScreenWidth + dstX] = bgra;
                }
            }
        }
    }

    void drawGlyph5x7(const Font& font, int x, int y, char ch, uint8_t color) {
        if (!font.contains(ch)) {
            return;
        }
        const int glyph = static_cast<unsigned char>(ch) - font.firstCode;
        const uint32_t bgra = toBgra(paletteColor(color));
        const int rows = std::min(7, font.height);
        const int cols = std::min(5, font.width);
        const int sourceColOffset = font.width > cols ? 1 : 0;
        for (int row = 0; row < rows; ++row) {
            const uint8_t bits = font.rows[static_cast<size_t>(glyph) * font.height + row];
            const int dstY = y + row;
            if (dstY < 0 || dstY >= kScreenHeight) {
                continue;
            }
            for (int col = 0; col < cols; ++col) {
                const int sourceCol = col + sourceColOffset;
                if (((bits >> (7 - sourceCol)) & 1) == 0) {
                    continue;
                }
                const int dstX = x + col;
                if (dstX >= 0 && dstX < kScreenWidth) {
                    framebuffer_[static_cast<size_t>(dstY) * kScreenWidth + dstX] = bgra;
                }
            }
        }
    }

    void drawGlyph7x5(const Font& font, int x, int y, char ch, uint8_t color) {
        if (!font.contains(ch)) {
            return;
        }
        const int glyph = static_cast<unsigned char>(ch) - font.firstCode;
        const uint32_t bgra = toBgra(paletteColor(color));
        const int rows = std::min(5, font.height);
        const int cols = std::min(7, font.width);
        const int sourceRowOffset = font.height > rows ? 1 : 0;
        for (int row = 0; row < rows; ++row) {
            const int sourceRow = row + sourceRowOffset;
            const uint8_t bits = font.rows[static_cast<size_t>(glyph) * font.height + sourceRow];
            const int dstY = y + row;
            if (dstY < 0 || dstY >= kScreenHeight) {
                continue;
            }
            for (int col = 0; col < cols; ++col) {
                if (((bits >> (7 - col)) & 1) == 0) {
                    continue;
                }
                const int dstX = x + col;
                if (dstX >= 0 && dstX < kScreenWidth) {
                    framebuffer_[static_cast<size_t>(dstY) * kScreenWidth + dstX] = bgra;
                }
            }
        }
    }

    void paint(HWND hwnd) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientW = client.right - client.left;
        const int clientH = client.bottom - client.top;
        const RectI displayRect = displayRectForClient(clientW, clientH);

        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        const RECT borderRects[] = {
            {0, 0, clientW, displayRect.y},
            {0, displayRect.y + displayRect.height, clientW, clientH},
            {0, displayRect.y, displayRect.x, displayRect.y + displayRect.height},
            {displayRect.x + displayRect.width, displayRect.y, clientW, displayRect.y + displayRect.height},
        };
        for (const RECT& rect : borderRects) {
            if (rect.left < rect.right && rect.top < rect.bottom) {
                FillRect(hdc, &rect, brush);
            }
        }
        DeleteObject(brush);

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kScreenWidth;
        info.bmiHeader.biHeight = -kScreenHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(
            hdc,
            displayRect.x,
            displayRect.y,
            displayRect.width,
            displayRect.height,
            0,
            0,
            kScreenWidth,
            kScreenHeight,
            framebuffer_.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);

        EndPaint(hwnd, &ps);
    }

    RectI displayRectForClient(int clientW, int clientH) const {
        int pixelW = 1;
        int pixelH = 1;
        const std::array<std::pair<int, int>, 5> displayModes = {{
            {kExactDisplayPixelWidth, kExactDisplayPixelHeight},
            {kCompactDisplayPixelWidth, kCompactDisplayPixelHeight},
            {3, 4},
            {2, 2},
            {1, 1},
        }};
        for (const auto& [candidateW, candidateH] : displayModes) {
            if (clientW >= kScreenWidth * candidateW &&
                clientH >= kScreenHeight * candidateH) {
                pixelW = candidateW;
                pixelH = candidateH;
                break;
            }
        }

        const int scale = std::max(
            1,
            std::min(
                clientW / (kScreenWidth * pixelW),
                clientH / (kScreenHeight * pixelH)));
        const int drawW = kScreenWidth * pixelW * scale;
        const int drawH = kScreenHeight * pixelH * scale;
        return {
            (clientW - drawW) / 2,
            (clientH - drawH) / 2,
            drawW,
            drawH,
        };
    }

#if defined(MW_DEBUG_TOOLS) && MW_DEBUG_TOOLS
    static constexpr size_t kDebugConsoleMaxInput = 80;
    DebugTools debugTools_;
    bool debugConsoleOpen_ = false;
    bool suppressNextDebugConsoleChar_ = false;
    std::wstring debugConsoleInput_;
#endif
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    fs::path resourceRoot_;
    PicsArchive archive_;
    PicsArchive activisionArchive_;
    PicsArchive titleArchive_;
    PicsArchive gpicsArchive_;
    PicsArchive campaignArchive_;
    PicsArchive barArchive_;
    PicsArchive barEnvironmentOverlayArchive_;
    PicsArchive barInformationArchive_;
    PicsArchive crewArchive_;
    PicsArchive crewMechArchive_;
    PicsArchive mechStatusArchive_;
    PicsArchive houseEmblemsArchive_;
    PicsArchive contractHouseNamesArchive_;
    PicsArchive contractHouseEmblemsArchive_;
    PicsArchive contractContactPortraitsArchive_;
    PicsArchive missionResultArchive_;
    PicsArchive travelShuttleArchive_;
    PicsArchive travelEngineArchive_;
    std::vector<uint8_t> mwMainData_;
    Font font_;
    Font smallFont_;
    Font menuFont_;
    std::vector<uint32_t> framebuffer_;
    std::vector<std::wstring> campaignMessageLines_;
    std::vector<StoryPage> storyPages_;
    std::vector<std::vector<std::wstring>> newsNetMessageLines_;
    std::vector<std::wstring> newsNetNoOtherLines_;
    std::vector<std::wstring> missionVictoryLines_;
    std::vector<std::wstring> missionDefeatLines_;
    std::vector<std::wstring> missionDeathPromptLines_;
    std::vector<size_t> activeNewsNetMessageIndexes_;
    std::vector<RecruitPilot> recruitPilots_;
    std::vector<PlanetRecruitPool> planetRecruitPools_;
    std::vector<int> recruitLastPlanetIndex_;
    std::vector<int> recruitLastMonthKey_;
    std::vector<size_t> previousPlanetRecruitIndexes_;
    std::vector<PlanetMechMarket> planetMechMarkets_;
    std::vector<int> contractNegotiationLockedVisitByPlanet_;
    std::vector<ContractOffer> activeContracts_;
    ContractOffer acceptedContract_;
    std::vector<MissionParticipant> missionParticipants_;
    std::vector<PlanetRecord> planets_;
    std::array<uint8_t, 256> storyMessageFlags_ = {};
    std::string startingStoryPlanetName_;
    std::wstring grigDestinationPlanet_;
    std::wstring wendallDestinationPlanet_;
    std::wstring kearneyDestinationPlanet_;
    std::wstring blackWidowDestinationPlanet_;
    std::wstring darkWingDestinationPlanet_;
    std::wstring status_;
    ScreenState state_ = ScreenState::ActivisionSplash;
    DWORD stateStartedTick_ = GetTickCount();
    size_t planetMenuIndex_ = kPlanetBarIconIndex;
    int currentPlanetIndex_ = 0;
    int selectedPlanetIndex_ = 0;
    int pendingTravelPlanetIndex_ = 0;
    uint64_t pendingTravelCost_ = 0;
    uint16_t pendingTravelDays_ = 0;
    size_t starmapButtonIndex_ = kStarmapNoButtonSelection;
    StarmapMenuMode starmapMenuMode_ = StarmapMenuMode::None;
    size_t starmapHouseSelectionIndex_ = 0;
    size_t starmapPlanetSelectionIndex_ = 0;
    bool starmapNameInputActive_ = false;
    int starmapNameInputOriginalPlanetIndex_ = 0;
    std::string starmapNameInput_;
    size_t statusMenuIndex_ = 0;
    size_t newsNetButtonIndex_ = 0;
    size_t contractMenuIndex_ = 0;
    size_t contractNegotiationButtonIndex_ = 1;
    size_t activeContractIndex_ = 0;
    ContractEditableField contractEditableField_ = ContractEditableField::None;
    bool contractAccepted_ = false;
    bool contractNegotiationTerminated_ = false;
    bool contractNegotiationUnavailable_ = false;
    bool missionLaunchPending_ = false;
    bool finalMissionStubActive_ = false;
    size_t battleStubButtonIndex_ = 0;
    MissionOutcome missionDebriefOutcome_ = MissionOutcome::Victory;
    uint64_t missionDebriefSalvage_ = 0;
    uint64_t missionDebriefPayment_ = 0;
    size_t missionDeathMenuIndex_ = 0;
    int newsNetMessageIndex_ = 0;
    bool newsNetShowingNoOther_ = false;
    int newsNetNoOtherDirection_ = 0;
    bool newsNetDayCharged_ = false;
    size_t currentStoryPageIndex_ = 0;
    size_t currentStoryChoiceIndex_ = 0;
    StoryAction pendingStoryDefaultAction_ = StoryAction::None;
    ScreenState pendingStoryReturnState_ = ScreenState::MainMenu;
    StoryBackdrop storyBackdrop_ = StoryBackdrop::Campaign;
    size_t mechLabMenuIndex_ = 0;
    size_t selectedMechIndex_ = 0;
    size_t mechStatusMenuIndex_ = 0;
    size_t repairSelectionIndex_ = 0;
    size_t reloadSelectionIndex_ = 0;
    size_t sellOfferSelectionIndex_ = 0;
    size_t selectedMarketMechIndex_ = 0;
    size_t mechMarketScrollOffset_ = 0;
    size_t mechBuyMenuIndex_ = 0;
    size_t extraAmmoSelectionIndex_ = 0;
    size_t barMenuIndex_ = 0;
    BarDialogState barDialogState_ = BarDialogState::None;
    int activeRecruitIndex_ = -1;
    size_t recruitChoiceIndex_ = 0;
    uint32_t recruitmentSeed_ = 0;
    int planetVisitSerialCounter_ = 1;
    int currentPlanetVisitSerial_ = 1;
    size_t systemMenuIndex_ = kSystemSaveMenuIndex;
    std::string saveGameNameInput_;
    std::vector<SaveGameSlot> restoreGameSlots_ = std::vector<SaveGameSlot>(kGamVisibleSlotCount);
    size_t restoreGameSelectionIndex_ = 0;
    std::vector<uint8_t> gamRawState_;
    bool gamRawStateValid_ = false;
    bool mechLabWeldingActive_ = false;
    DWORD mechLabNextWeldTick_ = 0;
    DWORD mechLabWeldStartedTick_ = 0;
    DWORD mechLabWeldDurationMs_ = kMechLabWeldDurations.front();
    size_t mechLabWeldSequence_ = 0;
    bool soundEnabled_ = true;
    int detailLevel_ = 0;
    std::wstring_view commanderName_ = L"G BRAVER";
    uint16_t playerReputationPoints_ = 0;
    uint8_t playerReputationTier_ = 0;
    uint64_t playerWealth_ = 1000000;
    std::array<int, 6> extraAmmoInHold_ = {};
    std::vector<OwnedMech> ownedMechs_ = {{makeStartingJenner()}};
    CrewInteractionMode crewInteractionMode_ = CrewInteractionMode::Navigate;
    size_t crewSelectionIndex_ = kCrewDoneSelectionIndex;
    size_t crewAssignmentIndex_ = 0;
    std::array<CrewMember, 4> crewMembers_ = {{
        {true, L"G BRAVER", L"POOR", L"POOR", 0, kCrewPlayerPortraitEntry},
        {},
        {},
        {},
    }};
    int currentYear_ = kStartingYear;
    int currentMonth_ = kStartingMonthZeroBased;
    int currentMonthDayCounter_ = kStartingMonthDayCounter;
    int currentPeriodic14DayCounter_ = kStartingPeriodic14DayCounter;
    int currentDay_ = 8;
    std::array<int, 5> housePositiveCounters_ = {};
    std::array<int, 5> houseNegativeCounters_ = {};
    std::array<int, 5> familyAttitudes_ = {{
        0,
        0,
        0,
        0,
        0,
    }};
    static constexpr std::array<std::wstring_view, 5> kHouseNames = {{
        L"KURITA :",
        L"STEINER:",
        L"MARIK  :",
        L"LIAO   :",
        L"DAVION :",
    }};
};

fs::path executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    while (true) {
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return fs::current_path();
        }
        if (size < buffer.size() - 1) {
            buffer.resize(size);
            return fs::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

fs::path findResourceRoot() {
    const fs::path cwd = fs::current_path();
    const fs::path exeDir = executableDirectory();
    const std::vector<fs::path> candidates = {
        cwd / L"Original",
        cwd,
        exeDir,
        exeDir / L"Original",
        exeDir.parent_path() / L"Original",
        exeDir.parent_path().parent_path() / L"Original",
    };
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate / L"MW_1PICS.BIN")) {
            return candidate;
        }
    }
    return cwd / L"Original";
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        SetProcessDPIAware();
        App app(findResourceRoot());
        if (!app.initialize(instance, showCommand)) {
            return 1;
        }
        return app.run();
    } catch (const std::exception& error) {
        std::wstring message = L"Fatal error: ";
        message += widen(error.what());
        MessageBoxW(nullptr, message.c_str(), L"MW Main Recomp", MB_ICONERROR);
        return 1;
    }
}
