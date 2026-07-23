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
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
    CrewMenu,
    Starmap,
    TravelRoutePreview,
    TravelAnimation,
};

enum class BarDialogState {
    None,
    RecruitOffer,
    NoCandidates,
    CrewFull,
};

enum class CrewInteractionMode {
    Navigate,
    AssignMech,
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
        SystemMenu,
        ToggleSound,
        Detail,
        Restart,
        Continue,
        ReviewMechs,
        ExtraAmmo,
        BuyMechs,
        ExitToDos,
    };

    struct MenuItem {
        std::wstring_view label;
        MenuAction action = MenuAction::None;
    };

    struct CrewMember {
        bool hired = false;
        std::wstring_view name;
        std::wstring_view gunnery;
        std::wstring_view piloting;
        uint32_t wage = 0;
        int portraitEntry = -1;
        int recruitIndex = -1;
    };

    enum class DamageState {
        Functional,
        LightDamage,
        HeavyDamage,
        Junk,
    };

    enum class ChassisId : size_t {
        Locust,
        Jenner,
        PhoenixHawk,
        ShadowHawk,
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
        std::array<int, 11> armorMax = {};
        std::array<int, 11> armorPoints = {};
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
        std::array<int, 11> armorMax = {};
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
    static constexpr size_t kPlanetStarmapIconIndex = 3;
    static constexpr size_t kPlanetBarIconIndex = 4;
    static constexpr size_t kPlanetSystemIconIndex = 5;
    static constexpr size_t kSystemContinueMenuIndex = 6;
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
    static constexpr uint64_t kMaxPlayerWealth = 10000000000ull;
    static constexpr int kMechLabBackgroundEntry = 2;
    static constexpr int kMechLabWeldX = 96;
    static constexpr int kMechLabWeldY = 64;
    static constexpr DWORD kMechLabWeldFrameMs = 60;
    static constexpr int kMechLabWeldEndingEntry = 33;
    static constexpr int kStarmapBackgroundEntry = 1;
    static constexpr int kCrewPlayerPortraitEntry = 22;
    static constexpr int kCrewDecorationEntry = 26;
    static constexpr size_t kPlayableMechCount = 8;
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
    static constexpr uint32_t kMechRepairArmorStepCost = 5000;
    static constexpr int kMechRepairArmorStepPoints = 5;
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
    static constexpr std::string_view kStartingPlanetName = "OSHIKA";
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
    static constexpr RectI kStarmapTravelButtonRect{245, 141, 60, 17};
    static constexpr RectI kStarmapPlanetsButtonRect{245, 160, 60, 17};
    static constexpr RectI kStarmapCancelButtonRect{245, 179, 60, 17};
    static constexpr RectI kNewsNetPreviousButtonRect{39, 181, 78, 16};
    static constexpr RectI kNewsNetNextButtonRect{122, 181, 76, 16};
    static constexpr RectI kNewsNetDoneButtonRect{205, 181, 76, 16};
    static constexpr uint8_t kNewsNetTextColor = 2;
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

    struct NewsNetEntry {
        MwMainTextRef text;
        int year = 0;
        int month = 0;
        int day = 0;
    };

    static constexpr std::array<NewsNetEntry, 13> kNewsNetEntries = {{
        {{"mw_main.news.01c0b4", 0x01C0B4u, 587u, 17u}, 3024, 4, 1},
        {{"mw_main.news.01c418", 0x01C418u, 494u, 15u}, 3024, 4, 8},
        {{"mw_main.news.01c60a", 0x01C60Au, 760u, 21u}, 3024, 4, 15},
        {{"mw_main.news.019f24", 0x019F24u, 618u, 18u}, 3024, 6, 30},
        {{"mw_main.news.01ad7d", 0x01AD7Du, 596u, 18u}, 3024, 7, 1},
        {{"mw_main.news.01a192", 0x01A192u, 374u, 12u}, 3024, 8, 25},
        {{"mw_main.news.01beee", 0x01BEEEu, 450u, 14u}, 3024, 11, 15},
        {{"mw_main.news.01c906", 0x01C906u, 191u, 6u}, 3025, 2, 14},
        {{"mw_main.news.01e383", 0x01E383u, 737u, 20u}, 3025, 3, 5},
        {{"mw_main.news.01e668", 0x01E668u, 647u, 18u}, 3025, 1, 22},
        {{"mw_main.news.01e8f3", 0x01E8F3u, 421u, 13u}, 3025, 4, 8},
        {{"mw_main.news.01ea9c", 0x01EA9Cu, 652u, 18u}, 3025, 4, 10},
        {{"mw_main.news.01f01f", 0x01F01Fu, 623u, 17u}, 3025, 4, 15},
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
        {L"AC 5-PKS", 285, {L"SHADOW HAWK", L"RIFLEMAN", L"MARAUDER", L""}, 3},
        {L"LRM 5-PKS", 1475, {L"SHADOW HAWK", L"", L"", L""}, 1},
        {L"SRM 2-PKS", 637, {L"SHADOW HAWK", L"", L"", L""}, 1},
        {L"SRM 4-PKS", 1274, {L"JENNER", L"", L"", L""}, 1},
        {L"SRM 6-PKS", 2124, {L"WARHAMMER", L"BATTLEMASTER", L"", L""}, 2},
        {L"MACH GUN", 5, {L"LOCUST", L"PHOENIX HAWK", L"WARHAMMER", L"BATTLEMASTER"}, 4},
    }};

    static constexpr std::array<MenuItem, 3> kStatusMenuItems = {{
        {L"CREW", MenuAction::Crew},
        {L"NEWS NET", MenuAction::NewsNet},
        {L"DONE", MenuAction::Continue},
    }};

    static constexpr std::array<MenuItem, 7> kSystemMenuItems = {{
        {L"SAVE GAME", MenuAction::None},
        {L"RESTORE GAME", MenuAction::None},
        {L"TURN SOUND OFF", MenuAction::ToggleSound},
        {L"DETAIL: LOW", MenuAction::Detail},
        {L"RESTART GAME", MenuAction::Restart},
        {L"EXIT TO DOS", MenuAction::ExitToDos},
        {L"CONTINUE", MenuAction::Continue},
    }};

    static constexpr std::array<size_t, 11> kArmorDamageOrder = {{
        4, 2, 6, 1, 7, 5, 3, 8, 9, 10, 0,
    }};

    static constexpr std::array<size_t, 11> kArmorRepairOrder = {{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    }};

    static constexpr std::array<MechDefinition, kPlayableMechCount> kMechDefinitions = {{
        {
            ChassisId::Locust,
            L"LOCUST",
            {15, 0x0003F115u, 68, 68, 0},
            {7, 0x0002A00Du, 111, 182, 1},
            20, 129, 0, 10, 0,
            {{8, 10, 8, 8, 4, 4, 8, 8, 2, 2, 2}},
            {{{L"M LAS", L"CT", DamageState::Functional}, {L"MG", L"RA", DamageState::Functional}, {L"MG", L"LA", DamageState::Functional}}},
            {{1504000, 1804000, 1955000, 2256000}},
            {{1353000, 1654000, 1804000, 2105000}},
            30,
        },
        {
            ChassisId::Jenner,
            L"JENNER",
            {16, 0x0003FA26u, 68, 68, 1},
            {8, 0x0002C7E6u, 125, 165, 1},
            35, 118, 150, 10, 3,
            {{9, 17, 13, 13, 12, 12, 15, 15, 4, 3, 3}},
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
            {{8, 23, 18, 18, 14, 14, 22, 22, 5, 4, 4}},
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
            {{9, 23, 18, 18, 16, 16, 16, 16, 8, 6, 6}},
            {{{L"AC/5", L"LT", DamageState::Functional}, {L"LRM5", L"RT", DamageState::Functional}, {L"SRM2", L"HD", DamageState::Functional}, {L"M LAS", L"RA", DamageState::Functional}}},
            {{4622000, 5546000, 6008000, 6933000}},
            {{4159000, 5084000, 5546000, 6470000}},
            13,
        },
        {
            ChassisId::Rifleman,
            L"RIFLEMAN",
            {19, 0x0004155Bu, 68, 68, 0},
            {11, 0x000344BBu, 135, 187, 0},
            60, 64, 0, 10, 0,
            {{6, 22, 15, 15, 15, 15, 12, 12, 4, 2, 2}},
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
            {{9, 22, 17, 17, 20, 20, 15, 15, 9, 8, 8}},
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
            {{9, 35, 17, 17, 22, 22, 18, 18, 10, 8, 8}},
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
            {{9, 40, 28, 28, 24, 24, 26, 26, 11, 8, 8}},
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
        mech.armorMax = definition.armorMax;
        mech.armorPoints = definition.armorMax;
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
        case ScreenState::CrewMenu:
            return L"CrewMenu";
        case ScreenState::Starmap:
            return L"Starmap";
        case ScreenState::TravelRoutePreview:
            return L"TravelRoutePreview";
        case ScreenState::TravelAnimation:
            return L"TravelAnimation";
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
                state_ == ScreenState::CrewMenu ||
                state_ == ScreenState::Starmap) {
                if (state_ == ScreenState::CrewMenu) {
                    changeState(ScreenState::StatusMenu);
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

        if (state_ == ScreenState::Starmap) {
            handleStarmapClick(screenX, screenY);
            return;
        }

        if (state_ == ScreenState::NewsNet) {
            handleNewsNetClick(screenX, screenY);
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

        if (state_ != ScreenState::MainMenu) {
            return;
        }

        const int iconIndex = hitPlanetIcon(screenX, screenY);
        if (iconIndex < 0) {
            return;
        }
        planetMenuIndex_ = static_cast<size_t>(iconIndex);
        activatePlanetIcon(planetMenuIndex_);
    }

    void activatePlanetIcon(size_t iconIndex) {
        if (iconIndex == kPlanetStatusIconIndex) {
            statusMenuIndex_ = 0;
            changeState(ScreenState::StatusMenu);
        } else if (iconIndex == kPlanetMechLabIconIndex) {
            mechLabMenuIndex_ = 0;
            changeState(ScreenState::MechLabMenu);
        } else if (iconIndex == kPlanetStarmapIconIndex) {
            if (!planets_.empty()) {
                selectedPlanetIndex_ = std::clamp(selectedPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
            }
            changeState(ScreenState::Starmap);
        } else if (iconIndex == kPlanetBarIconIndex) {
            changeState(ScreenState::BarMenu);
        } else if (iconIndex == kPlanetSystemIconIndex) {
            systemMenuIndex_ = kSystemContinueMenuIndex;
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
        if (hitRect(kStarmapCancelButtonRect, screenX, screenY)) {
            changeState(ScreenState::MainMenu);
            return;
        }
        if (hitRect(kStarmapTravelButtonRect, screenX, screenY)) {
            beginTravelToSelectedPlanet();
            return;
        }
        if (hitRect(kStarmapPlanetsButtonRect, screenX, screenY)) {
            return;
        }

        const int planetIndex = hitStarmapPlanet(screenX, screenY);
        if (planetIndex >= 0) {
            selectedPlanetIndex_ = planetIndex;
        }
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
            changeState(ScreenState::MainMenu);
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
            changeState(ScreenState::MainMenu);
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
        mech->armorPoints = mech->armorMax;
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
        if (action == MenuAction::RecruitCrew) {
            openRecruitDialog();
        } else if (action == MenuAction::Continue) {
            changeState(ScreenState::MainMenu);
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
        const uint8_t allowedSkill = static_cast<uint8_t>(std::min<int>(3, playerReputation_ + 1));
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
            changeState(ScreenState::MainMenu);
        } else if (action == MenuAction::Restart) {
            barMenuIndex_ = 0;
            planetMenuIndex_ = kPlanetBarIconIndex;
            systemMenuIndex_ = kSystemContinueMenuIndex;
            changeState(ScreenState::ActivisionSplash);
        } else if (action == MenuAction::ToggleSound) {
            soundEnabled_ = !soundEnabled_;
        } else if (action == MenuAction::Detail) {
            detailLevel_ = (detailLevel_ + 1) % 3;
        }
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
        playerReputation_ = std::min<uint8_t>(reputation, 3);
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
        changeState(ScreenState::NewsNet);
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
            changeState(ScreenState::StatusMenu);
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
            changeState(ScreenState::MainMenu);
            return true;
        }
        return false;
    }

    void handleKey(WPARAM key) {
        if (key == VK_ESCAPE) {
            if (state_ == ScreenState::SystemMenu) {
                changeState(ScreenState::MainMenu);
            } else if (state_ == ScreenState::StatusMenu) {
                changeState(ScreenState::MainMenu);
            } else if (state_ == ScreenState::NewsNet) {
                changeState(ScreenState::StatusMenu);
            } else if (state_ == ScreenState::CrewMenu) {
                if (crewInteractionMode_ == CrewInteractionMode::AssignMech) {
                    closeCrewAssignment();
                } else {
                    changeState(ScreenState::StatusMenu);
                }
            } else if (state_ == ScreenState::MechLabMenu) {
                changeState(ScreenState::MainMenu);
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
                    changeState(ScreenState::MainMenu);
                }
            } else if (state_ == ScreenState::Starmap) {
                changeState(ScreenState::MainMenu);
            } else if (state_ == ScreenState::TravelRoutePreview) {
                return;
            } else if (state_ == ScreenState::TravelAnimation) {
                return;
            } else if (state_ == ScreenState::MainMenu) {
                systemMenuIndex_ = kSystemContinueMenuIndex;
                changeState(ScreenState::SystemMenu);
            } else {
                DestroyWindow(hwnd_);
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

        if (key == VK_SPACE) {
            if (!advanceStartupScreen() && state_ == ScreenState::MainMenu) {
                activatePlanetIcon(planetMenuIndex_);
            }
            return;
        }

        if (state_ == ScreenState::MainMenu) {
            if (key == VK_UP || key == VK_LEFT) {
                planetMenuIndex_ = (planetMenuIndex_ + 5) % 6;
            } else if (key == VK_DOWN || key == VK_RIGHT || key == VK_TAB) {
                planetMenuIndex_ = (planetMenuIndex_ + 1) % 6;
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

        if (key == 'R') {
            loadResources();
            changeState(ScreenState::ActivisionSplash);
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
        crewArchive_ = {};
        crewMechArchive_ = {};
        mechStatusArchive_ = {};
        houseEmblemsArchive_ = {};
        travelShuttleArchive_ = {};
        travelEngineArchive_ = {};
        font_ = {};
        smallFont_ = {};
        menuFont_ = {};
        campaignMessageLines_.clear();
        newsNetMessageLines_.clear();
        newsNetNoOtherLines_.clear();
        activeNewsNetMessageIndexes_.clear();
        recruitPilots_.clear();
        planetRecruitPools_.clear();
        recruitLastPlanetIndex_.clear();
        recruitLastMonthKey_.clear();
        previousPlanetRecruitIndexes_.clear();
        planetMechMarkets_.clear();
        planets_.clear();
        barDialogState_ = BarDialogState::None;
        activeRecruitIndex_ = -1;
        recruitChoiceIndex_ = 0;
        resetCrewRoster();
        try {
            const fs::path mwMainPath = resourceRoot_ / L"MW_MAIN.EXE";
            const std::vector<uint8_t> mwMainData = readFile(mwMainPath);
            archive_ = loadPicsArchive(resourceRoot_ / L"MW_1PICS.BIN");
            activisionArchive_ = loadPicsArchive(resourceRoot_ / L"MW_APICS.BIN");
            titleArchive_ = loadPicsArchive(resourceRoot_ / L"MW_TPICS.BIN");
            gpicsArchive_ = loadPicsArchive(resourceRoot_ / L"MW_GPICS.BIN");
            crewArchive_ = loadPicsArchive(resourceRoot_ / L"MW_CPICS.BIN");
            campaignArchive_ = loadRawPicsImage(resourceRoot_ / L"MW_PICS.BIN", 0x00000280u, 1);
            loadMechArtArchives();
            travelShuttleArchive_ = loadRawPicsImage(resourceRoot_ / L"MW_PICS.BIN", 0x000680D5u, kTravelShuttleEntry);
            travelEngineArchive_ = loadPicsArchive(resourceRoot_ / L"MW_2PICS.BIN");
            barArchive_ = loadRawPicsImageWithNibblePhase(
                resourceRoot_ / L"MW_PICS.BIN",
                0x0001799Bu,
                2,
                kScreenWidth,
                kScreenHeight,
                true,
                1);
            smallFont_ = loadFont(resourceRoot_ / L"6X6.FNT");
            font_ = loadFont(resourceRoot_ / L"8X8B.FNT");
            menuFont_ = loadFont(resourceRoot_ / L"FOX88.FNT");
            campaignMessageLines_ = parseMwMainTextBlock(mwMainData, kCampaignIntroText);
            newsNetNoOtherLines_ = parseMwMainTextBlock(mwMainData, kNewsNetNoOtherText);
            newsNetMessageLines_.reserve(kNewsNetEntries.size());
            for (const NewsNetEntry& entry : kNewsNetEntries) {
                newsNetMessageLines_.push_back(parseMwMainTextBlock(mwMainData, entry.text));
            }
            recruitPilots_ = loadRecruitPilots(mwMainData);
            planets_ = loadPlanetRecords(mwMainPath);
            planetMechMarkets_.assign(planets_.size(), {});
            currentPlanetIndex_ = findPlanetIndexByName(kStartingPlanetName);
            selectedPlanetIndex_ = currentPlanetIndex_;
            pendingTravelPlanetIndex_ = currentPlanetIndex_;
            initializeRecruitmentState();
            loadHouseEmblems();
            std::wstringstream stream;
            stream << L"Loaded PICS: MW_1=" << archive_.images.size()
                   << L", MW_A=" << activisionArchive_.images.size()
                   << L", MW_T=" << titleArchive_.images.size()
                   << L", MW_G=" << gpicsArchive_.images.size()
                   << L", MW_C=" << crewArchive_.images.size()
                   << L", MW_PICS raw=" << campaignArchive_.images.size() + barArchive_.images.size() + crewMechArchive_.images.size() + mechStatusArchive_.images.size() + travelShuttleArchive_.images.size()
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

    void initializeRecruitmentState() {
        planetRecruitPools_.assign(planets_.size(), {});
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

        completeTravel();
        changeState(ScreenState::MainMenu);
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
        drawImageFullScreenOrCentered(campaignArchive_, 1);
        drawHeaderPanel(10, 10, 145, currentPlanetName());
        drawHeaderPanel(170, 10, 145, campaignDateLabel());
        drawCampaignButtons(introMessage);
    }

    void renderCampaignMessage() {
        renderCampaignShell(true);
        drawGpPanel(22, 64, 276, 76, 8);
        int y = 74;
        for (const std::wstring& line : campaignMessageLines_) {
            drawSmallTextPlain(31, y, line, 7);
            y += 8;
        }
    }

    void renderMainMenu() {
        renderCampaignShell();
    }

    void renderStatusMenu() {
        drawImageFullScreenOrCentered(campaignArchive_, 1);
        drawGpPanel(25, 20, 136, 102, 8);
        drawGpPanel(158, 130, 142, 58, 8);
        drawStatusInfoPanel(25, 20, 136);
        drawStatusActionPanel(158, 130, 142);
    }

    void renderNewsNet() {
        fillRect(0, 0, kScreenWidth, kScreenHeight, 0);
        fillRect(29, 23, 262, 154, 0);
        drawImageAt(gpicsArchive_, 12, 0, 0, false);
        drawImageAt(gpicsArchive_, 45, 0, 23, false);
        drawImageAt(gpicsArchive_, 46, 291, 23, false);
        drawImageAt(gpicsArchive_, 44, 0, 177, false);

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

        if (market.mechsForSale.empty()) {
            drawRecruitTextCenteredInRect(
                panel.x + 8,
                kMechBuyListFirstRowY + 28,
                panel.width - 16,
                L"SORRY, NO MECHS FOR SALE!",
                7);
            drawRecruitTextCenteredInRect(
                panel.x + 8,
                kMechBuyListFirstRowY + 38,
                panel.width - 16,
                L"TRY BACK NEXT WEEK.",
                7);
        } else {
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
        drawImageFullScreenOrCentered(barArchive_, 2);
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
        drawImageFullScreenOrCentered(campaignArchive_, 1);
        drawGpPanel(90, 80, 140, 86, 8);
        drawSystemMenu(90, 90, 140);
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

        drawStarmapText5x5(10, 8, widen(selectedPlanet.name), 15, 112);
        drawStarmapText5x5Centered(135, 8, 60, environmentLabel(selectedPlanet.terrainCode), 15);
        drawStarmapText5x5Centered(204, 8, 108, L"POP:" + formatWealth(selectedPlanet.population), 15);

        drawStarmapText5x5(10, 23, widen(selectedPlanet.description), 15, 300);

        const uint16_t jumpCount = travelJumpCount(currentPlanet, selectedPlanet);
        const uint64_t travelCost = travelCostForJumps(jumpCount);
        drawStarmapText5x5(240, 105, formatWealth(travelCost), 15, 70);
        drawStarmapText5x5(240, 129, formatWealth(playerWealth_), 15, 70);

        drawStarmapText5x5Centered(249, 147, 52, L"TRAVEL", 15);
        drawStarmapText5x5Centered(249, 166, 52, L"PLANETS", 15);
        drawStarmapText5x5Centered(249, 185, 52, L"CANCEL", 15);
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
        drawImageAt(gpicsArchive_, 5, 25, 57, true);
        drawImageAt(gpicsArchive_, 6, 25, 106, true);
        drawImageAt(gpicsArchive_, introMessage ? 26 : 30, 25, 156, true);

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

    static int armorMaxTotal(const OwnedMech& mech) {
        int total = 0;
        for (int value : mech.armorMax) {
            total += value;
        }
        return total;
    }

    static int armorPointTotal(const OwnedMech& mech) {
        int total = 0;
        for (int value : mech.armorPoints) {
            total += value;
        }
        return total;
    }

    static int armorMissingPoints(const OwnedMech& mech) {
        return std::max(0, armorMaxTotal(mech) - armorPointTotal(mech));
    }

    static void updateArmorPercent(OwnedMech& mech) {
        const int maxArmor = armorMaxTotal(mech);
        mech.armorPercent = maxArmor > 0 ? (armorPointTotal(mech) * 100 + maxArmor / 2) / maxArmor : 100;
    }

    static void damageArmorToPercent(OwnedMech& mech, int targetPercent) {
        const int maxArmor = armorMaxTotal(mech);
        const int clampedPercent = std::clamp(targetPercent, 0, 100);
        const int targetTotal = (maxArmor * clampedPercent + 50) / 100;
        int missing = std::max(0, maxArmor - targetTotal);

        mech.armorPoints = mech.armorMax;
        for (size_t section : kArmorDamageOrder) {
            if (missing <= 0 || section >= mech.armorPoints.size()) {
                break;
            }

            const int preferredFloor = std::max(0, mech.armorMax[section] / 3);
            const int removableToFloor = std::max(0, mech.armorPoints[section] - preferredFloor);
            const int removed = std::min(missing, removableToFloor);
            mech.armorPoints[section] -= removed;
            missing -= removed;
        }

        for (size_t section : kArmorDamageOrder) {
            if (missing <= 0 || section >= mech.armorPoints.size()) {
                break;
            }
            const int removed = std::min(missing, mech.armorPoints[section]);
            mech.armorPoints[section] -= removed;
            missing -= removed;
        }

        updateArmorPercent(mech);
    }

    static void repairArmorStep(OwnedMech& mech) {
        int remaining = kMechRepairArmorStepPoints;
        for (size_t section : kArmorRepairOrder) {
            if (remaining <= 0 || section >= mech.armorPoints.size()) {
                break;
            }
            const int missing = std::max(0, mech.armorMax[section] - mech.armorPoints[section]);
            const int repaired = std::min(remaining, missing);
            mech.armorPoints[section] += repaired;
            remaining -= repaired;
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
            {{570, 540, 70, 50, 8, 8, 12, 12}},
            {{42, 2, 90, 50, 10, 10, 15, 15}},
            {{232, 132, 110, 50, 13, 13, 19, 19}},
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
        int armorMax = 94;
        if (roll >= 76 && roll < 94) {
            targetState = DamageState::HeavyDamage;
            componentHits = 4;
            weaponHits = 2;
            armorMin = 35;
            armorMax = 72;
        } else if (roll >= 94) {
            targetState = DamageState::Junk;
            componentHits = 6;
            weaponHits = 3;
            armorMin = 12;
            armorMax = 45;
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
        std::uniform_int_distribution<int> armorDist(armorMin, armorMax);
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
            return armorMissingPoints(mech) > 0 ? kMechRepairArmorStepCost : 0;
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
        if (mech.armorPercent < 100) {
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
                const int missing = armorMissingPoints(mech);
                const int steps = (missing + kMechRepairArmorStepPoints - 1) / kMechRepairArmorStepPoints;
                total += static_cast<uint32_t>(steps) * kMechRepairArmorStepCost;
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
            return {1, -3, 0, 0};
        case ChassisId::Jenner:
            return {0, -2, 0, 0};
        case ChassisId::PhoenixHawk:
            return {1, -1, 0, 0};
        case ChassisId::ShadowHawk:
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

    static uint8_t armorSectionColor(int current, int maxValue) {
        if (maxValue <= 0) {
            return 7;
        }
        const int percent = current * 100 / maxValue;
        if (percent <= 50) {
            return 0;
        }
        if (percent <= 75) {
            return 14;
        }
        return 7;
    }

    static const std::vector<ArmorOverlayRect>& armorOverlayRects(ChassisId chassis) {
        static const std::array<std::vector<ArmorOverlayRect>, kPlayableMechCount> overlays = {{
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
        return overlays[chassisIndex(chassis)];
    }

    void drawArmorOverlay(const OwnedMech& mech, int imageX, int imageY) {
        const uint32_t magenta = toBgra(paletteColor(5));
        const uint32_t brightMagenta = toBgra(paletteColor(13));

        for (const ArmorOverlayRect& overlayRect : armorOverlayRects(mech.chassis)) {
            const size_t section = overlayRect.section;
            if (section >= mech.armorPoints.size() || section >= mech.armorMax.size()) {
                continue;
            }

            const RectI& imageRect = overlayRect.rect;
            const uint32_t target =
                toBgra(paletteColor(armorSectionColor(mech.armorPoints[section], mech.armorMax[section])));
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
        drawTextPass(smallFont_, x + 80, y + 32, std::wstring(L" ") + std::wstring(reputationLabel()), 7);
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
        switch (playerReputation_) {
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
        const int clamped = std::clamp(score, 0, 20);
        if (clamped <= 5) {
            return L"NEGATIVE";
        }
        if (clamped <= 10) {
            return L"NEUTRAL";
        }
        if (clamped <= 16) {
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
        const int currentDay = std::max(1, currentMonthDayCounter_);
        if (currentYear_ != entry.year) {
            return currentYear_ > entry.year;
        }
        if (currentMonthOneBased != entry.month) {
            return currentMonthOneBased > entry.month;
        }
        return currentDay >= entry.day;
    }

    void rebuildNewsNetMessages() {
        activeNewsNetMessageIndexes_.clear();
        for (size_t i = 0; i < kNewsNetEntries.size(); ++i) {
            if (isNewsNetEntryAvailable(kNewsNetEntries[i])) {
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
            return widen(kStartingPlanetName);
        }
        const int index = std::clamp(currentPlanetIndex_, 0, static_cast<int>(planets_.size()) - 1);
        return widen(planets_[index].name);
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

    std::wstring_view environmentLabel(uint8_t terrainCode) const {
        switch (terrainCode % 3u) {
        case 1:
            return L"DESERT";
        case 2:
            return L"TROPICAL";
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
    PicsArchive crewArchive_;
    PicsArchive crewMechArchive_;
    PicsArchive mechStatusArchive_;
    PicsArchive houseEmblemsArchive_;
    PicsArchive travelShuttleArchive_;
    PicsArchive travelEngineArchive_;
    Font font_;
    Font smallFont_;
    Font menuFont_;
    std::vector<uint32_t> framebuffer_;
    std::vector<std::wstring> campaignMessageLines_;
    std::vector<std::vector<std::wstring>> newsNetMessageLines_;
    std::vector<std::wstring> newsNetNoOtherLines_;
    std::vector<size_t> activeNewsNetMessageIndexes_;
    std::vector<RecruitPilot> recruitPilots_;
    std::vector<PlanetRecruitPool> planetRecruitPools_;
    std::vector<int> recruitLastPlanetIndex_;
    std::vector<int> recruitLastMonthKey_;
    std::vector<size_t> previousPlanetRecruitIndexes_;
    std::vector<PlanetMechMarket> planetMechMarkets_;
    std::vector<PlanetRecord> planets_;
    std::wstring status_;
    ScreenState state_ = ScreenState::ActivisionSplash;
    DWORD stateStartedTick_ = GetTickCount();
    size_t planetMenuIndex_ = kPlanetBarIconIndex;
    int currentPlanetIndex_ = 0;
    int selectedPlanetIndex_ = 0;
    int pendingTravelPlanetIndex_ = 0;
    uint64_t pendingTravelCost_ = 0;
    uint16_t pendingTravelDays_ = 0;
    size_t statusMenuIndex_ = 0;
    size_t newsNetButtonIndex_ = 0;
    int newsNetMessageIndex_ = 0;
    bool newsNetShowingNoOther_ = false;
    int newsNetNoOtherDirection_ = 0;
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
    size_t systemMenuIndex_ = kSystemContinueMenuIndex;
    bool mechLabWeldingActive_ = false;
    DWORD mechLabNextWeldTick_ = 0;
    DWORD mechLabWeldStartedTick_ = 0;
    DWORD mechLabWeldDurationMs_ = kMechLabWeldDurations.front();
    size_t mechLabWeldSequence_ = 0;
    bool soundEnabled_ = true;
    int detailLevel_ = 0;
    std::wstring_view commanderName_ = L"G BRAVER";
    uint8_t playerReputation_ = 0;
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
    std::array<int, 5> familyAttitudes_ = {{
        10,
        10,
        10,
        10,
        10,
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
