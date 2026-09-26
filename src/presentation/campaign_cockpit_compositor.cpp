#include "presentation/campaign_cockpit_compositor.h"

#include "legacy3d/shape_parser.h"
#include "legacy3d/terrain_parser.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace mw::presentation {

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 200;
constexpr int kSpeedGaugeX = 236;
constexpr int kSpeedGaugeY = 177;
constexpr int kHeavySpeedGaugeX = 22;
constexpr int kHeavySpeedGaugeY = 186;
constexpr int kSpeedGaugeBarCount = 31;
constexpr int kSpeedGaugeZeroIndex = 9;
constexpr int kSpeedGaugeMaxForwardBars = 16;
constexpr int kHeatGaugeX = 198;
constexpr int kHeatGaugeBottomY = 183;
constexpr int kJumpGaugeRightInset = 9;
constexpr int kJumpGaugeTopInset = 16;
constexpr int kJumpGaugeWidth = 7;
constexpr int kJumpGaugeHeight = 40;
constexpr int kJumpReadyLightRightInset = 19;
constexpr int kJumpReadyLightTopInset = 5;

constexpr CampaignCockpitLayout kLayouts[] = {
    {
        battle::CampaignCockpitFamily::Light,
        "light",
        {11, 11, 297, 92},
        {8, 112, 94, 66},
        {115, 116, 90, 68},
        {213, 116, 95, 66},
        {64, 103, 192, 12},
    },
    {
        battle::CampaignCockpitFamily::Medium,
        "medium",
        {9, 0, 301, 103},
        {3, 110, 104, 70},
        {113, 116, 94, 66},
        {214, 110, 104, 70},
        {64, 103, 192, 12},
    },
    {
        battle::CampaignCockpitFamily::Heavy,
        "heavy",
        {0, 0, 320, 103},
        {4, 113, 106, 75},
        {120, 117, 72, 67},
        {206, 112, 111, 76},
        {96, 103, 128, 12},
    },
};

constexpr CampaignCockpitWeaponPanelLayout kWeaponPanelLayouts[] = {
    // LIGHT.SCR uses two pixels of left padding in the name cell. Its ammo
    // digit and range letter sit one pixel left/right of the medium positions.
    {false, 19, 125, 6, 5, 29, 81, 86, 8, 5, 0, 5},
    // MEDIUM.SCR uses the same corrected five-row field alignment.
    {false, 19, 125, 6, 5, 29, 81, 86, 8, 5, 0, 5},
    // HEAVY.SCR carries the ten-row weapon grid on the right.
    // Its lower five-row bank begins three pixels below the regular cadence.
    {true, 233, 112, 6, 5, 243, 295, 300, 8, 5, 3, 10},
};

constexpr CampaignCockpitHudColor kHudColors[] = {
    {"green", 0xff00aa00u, {0.0f, 0.67f, 0.0f}},
    {"black", 0xff000000u, {0.0f, 0.0f, 0.0f}},
    {"red", 0xffaa0000u, {0.67f, 0.0f, 0.0f}},
    {"cyan", 0xff55ffffu, {85.0f / 255.0f, 1.0f, 1.0f}},
    {"brown", 0xffaa5500u, {0.67f, 0.33f, 0.0f}},
    {"gray", 0xffaaaaaau, {0.67f, 0.67f, 0.67f}},
    {"darkgray", 0xff555555u, {0.33f, 0.33f, 0.33f}},
    {"blue", 0xff0000aau, {0.0f, 0.0f, 0.67f}},
    {"brightgreen", 0xff55ff55u, {85.0f / 255.0f, 1.0f, 85.0f / 255.0f}},
    {"brightcyan", 0xff55ffffu, {85.0f / 255.0f, 1.0f, 1.0f}},
    {"lightred", 0xffff5555u, {1.0f, 85.0f / 255.0f, 85.0f / 255.0f}},
    {"brightmagenta", 0xffff55ffu, {1.0f, 85.0f / 255.0f, 1.0f}},
    {"yellow", 0xffffff55u, {1.0f, 1.0f, 85.0f / 255.0f}},
    {"white", 0xffffffffu, {1.0f, 1.0f, 1.0f}},
    {"black2", 0xff000000u, {0.0f, 0.0f, 0.0f}},
    {"darkblue", 0xff000055u, {0.0f, 0.0f, 0.33f}},
    {"darkgreen", 0xff005500u, {0.0f, 0.33f, 0.0f}},
};

uint32_t readU32Le(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4u > data.size()) {
        throw std::runtime_error("unexpected end of tagged cockpit resource");
    }
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1u]) << 8u) |
           (static_cast<uint32_t>(data[offset + 2u]) << 16u) |
           (static_cast<uint32_t>(data[offset + 3u]) << 24u);
}

bool isKnownTag(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4u > data.size()) {
        return false;
    }
    const std::string tag(reinterpret_cast<const char*>(data.data() + offset), 4u);
    return tag == "SCR:" || tag == "BIN:" || tag == "INF:" || tag == "BMP:" ||
           tag == "PAL:" || tag == "FNT:" || tag == "EGA:" || tag == "CGA:" ||
           tag == "IBM:";
}

bool findTaggedPayload(
    const std::vector<uint8_t>& data,
    const std::string& wantedTag,
    size_t start,
    size_t limit,
    size_t& payloadOffset,
    size_t& payloadSize) {
    size_t offset = start;
    while (offset + 8u <= limit && isKnownTag(data, offset)) {
        const std::string tag(reinterpret_cast<const char*>(data.data() + offset), 4u);
        const uint32_t rawLength = readU32Le(data, offset + 4u);
        const size_t size = static_cast<size_t>(rawLength & 0x7fffffffu);
        const bool nested = (rawLength & 0x80000000u) != 0u;
        const size_t currentPayloadOffset = offset + 8u;
        const size_t endOffset = currentPayloadOffset + size;
        if (endOffset > limit || endOffset > data.size()) {
            throw std::runtime_error("tagged cockpit resource exceeds file size");
        }
        if (tag == wantedTag) {
            payloadOffset = currentPayloadOffset;
            payloadSize = size;
            return true;
        }
        if (nested && findTaggedPayload(
                          data,
                          wantedTag,
                          currentPayloadOffset,
                          endOffset,
                          payloadOffset,
                          payloadSize)) {
            return true;
        }
        offset = endOffset;
    }
    return false;
}

std::vector<uint8_t> loadBytes(const std::filesystem::path& path, const char* kind) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(std::string("could not open cockpit ") + kind + ": " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::array<std::array<uint8_t, 3>, 16> defaultEgaPalette() {
    return {{
        {{0x00, 0x00, 0x00}}, {{0x00, 0x00, 0xaa}},
        {{0x00, 0xaa, 0x00}}, {{0x00, 0xaa, 0xaa}},
        {{0xaa, 0x00, 0x00}}, {{0xaa, 0x00, 0xaa}},
        {{0xaa, 0x55, 0x00}}, {{0xaa, 0xaa, 0xaa}},
        {{0x55, 0x55, 0x55}}, {{0x55, 0x55, 0xff}},
        {{0x55, 0xff, 0x55}}, {{0x55, 0xff, 0xff}},
        {{0xff, 0x55, 0x55}}, {{0xff, 0x55, 0xff}},
        {{0xff, 0xff, 0x55}}, {{0xff, 0xff, 0xff}},
    }};
}

std::array<std::array<uint8_t, 3>, 16> loadPalette(
    const std::filesystem::path& path) {
    const std::vector<uint8_t> data = loadBytes(path, "PAL");
    size_t offset = 0;
    size_t size = 0;
    if (!findTaggedPayload(data, "EGA:", 0u, data.size(), offset, size)) {
        throw std::runtime_error("cockpit PAL has no EGA chunk: " + path.string());
    }
    if (size != 128u) {
        throw std::runtime_error("cockpit PAL EGA chunk is not 128 bytes: " + path.string());
    }
    const auto ega = defaultEgaPalette();
    std::array<std::array<uint8_t, 3>, 16> palette{};
    for (size_t i = 0; i < palette.size(); ++i) {
        palette[i] = ega[data[offset + i * 2u] & 0x0fu];
    }
    return palette;
}

class LsbBitReader {
public:
    explicit LsbBitReader(const std::vector<uint8_t>& data) : data_(data) {}

    std::optional<int> read(int width) {
        if (bitIndex_ + static_cast<size_t>(width) > data_.size() * 8u) {
            return std::nullopt;
        }
        int value = 0;
        for (int i = 0; i < width; ++i) {
            value |= static_cast<int>((data_[bitIndex_ / 8u] >> (bitIndex_ % 8u)) & 1u) << i;
            ++bitIndex_;
        }
        return value;
    }

    size_t consumedBytes() const {
        return (bitIndex_ + 7u) / 8u;
    }

private:
    const std::vector<uint8_t>& data_;
    size_t bitIndex_ = 0;
};

std::vector<uint8_t> decompressCodec1(
    const std::vector<uint8_t>& body,
    size_t expectedSize) {
    std::vector<uint8_t> output;
    output.reserve(expectedSize);
    size_t offset = 0;
    while (offset < body.size()) {
        const uint8_t command = body[offset++];
        if ((command & 0x80u) != 0u) {
            const size_t count = command & 0x7fu;
            if (offset >= body.size()) {
                throw std::runtime_error("cockpit SCR codec 1 run is missing value");
            }
            output.insert(output.end(), count, body[offset++]);
        } else {
            const size_t count = command;
            if (offset + count > body.size()) {
                throw std::runtime_error("cockpit SCR codec 1 literal exceeds input");
            }
            output.insert(output.end(), body.begin() + static_cast<std::ptrdiff_t>(offset),
                          body.begin() + static_cast<std::ptrdiff_t>(offset + count));
            offset += count;
        }
    }
    if (output.size() != expectedSize) {
        throw std::runtime_error("cockpit SCR codec 1 decoded size mismatch");
    }
    return output;
}

std::vector<uint8_t> decompressCodec2(
    const std::vector<uint8_t>& body,
    size_t expectedSize) {
    LsbBitReader reader(body);
    int width = 9;
    int nextCode = 257;
    std::map<int, std::vector<uint8_t>> dictionary;
    for (int i = 0; i < 256; ++i) {
        dictionary.emplace(i, std::vector<uint8_t>{static_cast<uint8_t>(i)});
    }

    std::vector<uint8_t> output;
    output.reserve(expectedSize);
    std::vector<uint8_t> previous;
    bool hasPrevious = false;
    while (const std::optional<int> code = reader.read(width)) {
        std::vector<uint8_t> current;
        const auto found = dictionary.find(*code);
        if (found != dictionary.end()) {
            current = found->second;
        } else if (hasPrevious && *code == nextCode) {
            current = previous;
            current.push_back(previous.front());
        } else {
            throw std::runtime_error("cockpit SCR codec 2 bad LZW code");
        }
        output.insert(output.end(), current.begin(), current.end());
        if (hasPrevious) {
            std::vector<uint8_t> entry = previous;
            entry.push_back(current.front());
            dictionary.emplace(nextCode++, std::move(entry));
            if (nextCode >= (1 << width) && width < 12) {
                ++width;
            }
        }
        previous = std::move(current);
        hasPrevious = true;
    }
    if (reader.consumedBytes() != body.size() || output.size() != expectedSize) {
        throw std::runtime_error("cockpit SCR codec 2 decoded size mismatch");
    }
    return output;
}

std::vector<uint8_t> decodeBin(
    const std::vector<uint8_t>& payload,
    int& codec) {
    if (payload.size() < 5u) {
        throw std::runtime_error("cockpit SCR BIN payload is too small");
    }
    codec = payload[0];
    const size_t expectedSize = static_cast<size_t>(readU32Le(payload, 1u));
    const std::vector<uint8_t> body(payload.begin() + 5, payload.end());
    if (codec == 1) {
        return decompressCodec1(body, expectedSize);
    }
    if (codec == 2) {
        return decompressCodec2(body, expectedSize);
    }
    throw std::runtime_error("unsupported cockpit SCR BIN codec");
}

std::pair<std::vector<int>, std::vector<int>> decodeBmpDimensions(
    const std::vector<uint8_t>& payload) {
    if (payload.size() < 6u || (payload.size() % 2u) != 0u) {
        throw std::runtime_error("cockpit BMP INF payload has invalid size");
    }
    const auto readU16 = [&payload](size_t offset) {
        if (offset + 2u > payload.size()) {
            throw std::runtime_error("cockpit BMP INF read past end");
        }
        return static_cast<int>(payload[offset]) |
               (static_cast<int>(payload[offset + 1u]) << 8);
    };
    const int count = readU16(0u);
    if (count <= 0 || payload.size() != 2u + static_cast<size_t>(count) * 4u) {
        throw std::runtime_error("cockpit BMP INF dimensions do not match record count");
    }
    std::vector<int> widths;
    std::vector<int> heights;
    widths.reserve(static_cast<size_t>(count));
    heights.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int width = readU16(2u + static_cast<size_t>(i) * 2u);
        const int height = readU16(2u + static_cast<size_t>(count + i) * 2u);
        if (width <= 0 || width > 640 || height <= 0 || height > 400) {
            throw std::runtime_error("cockpit BMP INF contains invalid dimensions");
        }
        widths.push_back(width);
        heights.push_back(height);
    }
    return {widths, heights};
}

std::vector<CampaignIndexedSprite> loadIndexedSprites(
    const std::filesystem::path& path) {
    const std::vector<uint8_t> data = loadBytes(path, "BMP");
    size_t infOffset = 0;
    size_t infSize = 0;
    if (!findTaggedPayload(data, "INF:", 0u, data.size(), infOffset, infSize)) {
        throw std::runtime_error("cockpit BMP has no INF chunk: " + path.string());
    }
    const std::vector<uint8_t> inf(
        data.begin() + static_cast<std::ptrdiff_t>(infOffset),
        data.begin() + static_cast<std::ptrdiff_t>(infOffset + infSize));
    const auto [widths, heights] = decodeBmpDimensions(inf);

    size_t binOffset = 0;
    size_t binSize = 0;
    if (!findTaggedPayload(data, "BIN:", 0u, data.size(), binOffset, binSize)) {
        throw std::runtime_error("cockpit BMP has no BIN chunk: " + path.string());
    }
    const std::vector<uint8_t> bin(
        data.begin() + static_cast<std::ptrdiff_t>(binOffset),
        data.begin() + static_cast<std::ptrdiff_t>(binOffset + binSize));
    int codec = 0;
    const std::vector<uint8_t> decoded = decodeBin(bin, codec);
    (void)codec;

    std::vector<CampaignIndexedSprite> sprites;
    sprites.reserve(widths.size());
    size_t sourceOffset = 0;
    for (size_t spriteIndex = 0; spriteIndex < widths.size(); ++spriteIndex) {
        CampaignIndexedSprite sprite;
        sprite.index = static_cast<int>(spriteIndex);
        sprite.width = widths[spriteIndex];
        sprite.height = heights[spriteIndex];
        sprite.rowStride = (sprite.width + 1) / 2;
        const size_t recordSize =
            static_cast<size_t>(sprite.rowStride) * static_cast<size_t>(sprite.height);
        if (sourceOffset + recordSize > decoded.size()) {
            throw std::runtime_error(
                "cockpit BMP decoded data is shorter than INF dimensions require");
        }
        sprite.packedPixels.assign(
            decoded.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
            decoded.begin() + static_cast<std::ptrdiff_t>(sourceOffset + recordSize));
        sprites.push_back(std::move(sprite));
        sourceOffset += recordSize;
    }
    if (sourceOffset != decoded.size()) {
        throw std::runtime_error("cockpit BMP decoded data has trailing bytes");
    }
    return sprites;
}

std::vector<CampaignCockpitSprite> loadSprites(
    const std::filesystem::path& path,
    const std::filesystem::path& palettePath) {
    const std::vector<CampaignIndexedSprite> indexedSprites =
        loadIndexedSprites(path);
    const auto palette = loadPalette(palettePath);

    std::vector<CampaignCockpitSprite> sprites;
    sprites.reserve(indexedSprites.size());
    for (const CampaignIndexedSprite& indexed : indexedSprites) {
        CampaignCockpitSprite sprite;
        sprite.index = indexed.index;
        sprite.width = indexed.width;
        sprite.height = indexed.height;
        sprite.rgbaPixels.resize(
            static_cast<size_t>(indexed.width) * static_cast<size_t>(indexed.height) * 4u);
        size_t pixelIndex = 0;
        for (int y = 0; y < indexed.height; ++y) {
            for (int x = 0; x < indexed.width; ++x) {
                const uint8_t packed = indexed.packedPixels[
                    static_cast<size_t>(y) * static_cast<size_t>(indexed.rowStride) +
                    static_cast<size_t>(x / 2)];
                const uint8_t color = (x % 2 == 0)
                                          ? static_cast<uint8_t>((packed >> 4u) & 0x0fu)
                                          : static_cast<uint8_t>(packed & 0x0fu);
                const auto& rgb = palette[color];
                const size_t destination = pixelIndex++ * 4u;
                sprite.rgbaPixels[destination] = rgb[0];
                sprite.rgbaPixels[destination + 1u] = rgb[1];
                sprite.rgbaPixels[destination + 2u] = rgb[2];
                sprite.rgbaPixels[destination + 3u] = color == 0u ? 0x00u : 0xffu;
            }
        }
        sprites.push_back(std::move(sprite));
    }
    return sprites;
}

CampaignCockpitFont loadFont(const std::filesystem::path& path) {
    std::vector<uint8_t> data = loadBytes(path, "FNT");
    std::vector<uint8_t> payload;
    size_t offset = 0;
    size_t size = 0;
    if (data.size() >= 8u && isKnownTag(data, 0u) &&
        findTaggedPayload(data, "FNT:", 0u, data.size(), offset, size)) {
        payload.assign(
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + size));
    } else {
        payload = std::move(data);
    }
    if (payload.size() < 4u) {
        throw std::runtime_error("cockpit FNT payload is too small: " + path.string());
    }
    CampaignCockpitFont font;
    font.width = payload[0];
    font.height = payload[1];
    font.firstCode = payload[2];
    font.glyphCount = payload[3];
    const size_t expected = 4u + static_cast<size_t>(font.glyphCount) * font.height;
    if (font.width <= 0 || font.width > 8 || font.height <= 0 || font.height > 16 ||
        font.glyphCount <= 0 || payload.size() != expected) {
        throw std::runtime_error("cockpit FNT payload has unsupported layout: " + path.string());
    }
    font.rows.assign(payload.begin() + 4, payload.end());
    return font;
}

} // namespace

const CampaignCockpitLayout& campaignCockpitLayout(
    battle::CampaignCockpitFamily family) {
    for (const CampaignCockpitLayout& layout : kLayouts) {
        if (layout.family == family) {
            return layout;
        }
    }
    return kLayouts[0];
}

const CampaignCockpitWeaponPanelLayout& campaignCockpitWeaponPanelLayout(
    battle::CampaignCockpitFamily family) {
    const size_t index = static_cast<size_t>(family);
    return index < std::size(kWeaponPanelLayouts)
        ? kWeaponPanelLayouts[index]
        : kWeaponPanelLayouts[0];
}

int campaignCockpitWeaponPanelRowY(
    const CampaignCockpitWeaponPanelLayout& layout,
    size_t row) {
    return layout.firstRowY + static_cast<int>(row) * layout.rowStep +
        (row >= layout.secondBankFirstRow ? layout.secondBankYOffset : 0);
}

CampaignCockpitWeaponTextColors campaignCockpitWeaponTextColors(
    battle::CampaignCockpitFamily family,
    battle::BattleWeaponReadiness readiness,
    bool selectedTargetWithinMaximumRange,
    bool cooldownActive) {
    constexpr uint32_t kBlack = 0xff000000u;
    constexpr uint32_t kRed = 0xffaa0000u;
    constexpr uint32_t kYellow = 0xffffff55u;
    constexpr uint32_t kWhite = 0xffffffffu;
    const bool unavailable =
        readiness == battle::BattleWeaponReadiness::NoAmmunition ||
        readiness == battle::BattleWeaponReadiness::NonFunctional;
    const bool coolingDown =
        cooldownActive ||
        readiness == battle::BattleWeaponReadiness::Cooldown;
    return {
        unavailable
            ? (family == battle::CampaignCockpitFamily::Heavy ? kBlack : kWhite)
            : coolingDown
                ? kRed
                : kWhite,
        selectedTargetWithinMaximumRange ? kYellow : kBlack,
    };
}

bool campaignCockpitWeaponRangeIndicatorActive(
    const battle::BattleSnapshot& snapshot,
    const battle::BattleWeaponInstanceState& weapon) {
    if (!battle::battleTargetScanHasSelection(snapshot.targetScan) ||
        !weapon.originalRangeValueProven || weapon.maximumRange <= 0.0) {
        return false;
    }
    const double targetDistanceWorld =
        snapshot.targetScan.selectedTargetDistanceMeters *
        battle::battleTargetScanWorldUnitsPerMeter();
    return targetDistanceWorld <= weapon.maximumRange;
}

double campaignCockpitDisplayHeadingDegrees(double runtimeHeadingRadians) {
    constexpr double kPi = 3.14159265358979323846;
    double degrees = std::fmod(
        (kPi - runtimeHeadingRadians) * 180.0 / kPi,
        360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

CampaignEgaPaletteBank loadCampaignEgaPaletteBank(
    const std::filesystem::path& palettePath,
    size_t bankIndex) {
    CampaignEgaPaletteBank bank;
    bank.bankIndex = bankIndex;
    if (bankIndex >= 4u) {
        bank.reason = "campaign_ega_palette_bank_out_of_range";
        return bank;
    }
    try {
        const std::vector<uint8_t> data = loadBytes(palettePath, "PAL");
        size_t offset = 0;
        size_t size = 0;
        if (!findTaggedPayload(data, "EGA:", 0u, data.size(), offset, size)) {
            throw std::runtime_error("cockpit PAL has no EGA chunk: " + palettePath.string());
        }
        if (size != 128u) {
            throw std::runtime_error("cockpit PAL EGA chunk is not 128 bytes: " + palettePath.string());
        }
        const size_t bankOffset = offset + bankIndex * 32u;
        for (size_t index = 0; index < bank.colorPairs.size(); ++index) {
            bank.colorPairs[index] = data[bankOffset + index * 2u];
        }
        bank.valid = true;
    } catch (const std::exception& error) {
        bank.reason = error.what();
    }
    return bank;
}

CampaignEgaPaletteColors loadCampaignEgaPaletteColors(
    const std::filesystem::path& palettePath) {
    CampaignEgaPaletteColors colors;
    try {
        const auto palette = loadPalette(palettePath);
        for (size_t i = 0; i < palette.size(); ++i) {
            colors.bgra[i] = 0xff000000u |
                (static_cast<uint32_t>(palette[i][0]) << 16u) |
                (static_cast<uint32_t>(palette[i][1]) << 8u) |
                static_cast<uint32_t>(palette[i][2]);
        }
        colors.valid = true;
    } catch (const std::exception& error) {
        colors.reason = error.what();
    }
    return colors;
}

CampaignCockpitRect campaignMissionStatusMapRect() {
    return {5, 15, 197, 107};
}

CampaignCockpitRect campaignCockpitCommandMapRect() {
    return {62, 7, 197, 107};
}

CampaignCockpitRect campaignCockpitMinimapRect() {
    return {120, 117, 72, 67};
}

CampaignCockpitRadarGeometry campaignCockpitRadarGeometry() {
    CampaignCockpitRadarGeometry geometry;
    geometry.rect = campaignCockpitMinimapRect();
    geometry.leftX = geometry.rect.x + 2;
    geometry.rightX = geometry.rect.x + geometry.rect.width - 3;
    geometry.topY = geometry.rect.y + 2;
    geometry.apexX = geometry.rect.x + geometry.rect.width / 2;
    geometry.apexY = geometry.rect.y + 43;
    geometry.labelY = geometry.rect.y + 56;
    return geometry;
}

CampaignCockpitRadarContactProjection campaignCockpitRadarContactProjection(
    const battle::Transform& player,
    const battle::Transform& contact,
    uint16_t rangeMeters) {
    CampaignCockpitRadarContactProjection projection;
    if (rangeMeters == 0u) {
        return projection;
    }

    const double deltaX = contact.x - player.x;
    const double deltaZ = contact.z - player.z;
    const double headingSin = std::sin(player.headingRadians);
    const double headingCos = std::cos(player.headingRadians);
    const double forward = deltaX * headingSin + deltaZ * headingCos;
    const double right = deltaX * headingCos - deltaZ * headingSin;
    const double rangeWorld =
        static_cast<double>(rangeMeters) * campaignCockpitRadarWorldUnitsPerMeter();
    const double distanceWorld = std::hypot(deltaX, deltaZ);
    projection.distanceMeters =
        distanceWorld / campaignCockpitRadarWorldUnitsPerMeter();

    // Captures show a 90-degree forward V sector. Rear contacts and contacts
    // outside either edge fail closed until a wider original scan is proven.
    if (forward < 0.0 || std::abs(right) > forward ||
        distanceWorld > rangeWorld) {
        return projection;
    }

    const CampaignCockpitRadarGeometry geometry =
        campaignCockpitRadarGeometry();
    const double verticalSpan =
        static_cast<double>(geometry.apexY - geometry.topY);
    const double horizontalSpan =
        static_cast<double>(geometry.rightX - geometry.leftX) * 0.5;
    // The battle camera's visible-right direction is the negative of the
    // legacy world-space lateral basis used above. Preserve the established
    // cockpit heading convention when mapping that basis to MFD screen X.
    projection.x = geometry.apexX - static_cast<int>(std::lround(
        (right / rangeWorld) * horizontalSpan));
    projection.y = geometry.apexY - static_cast<int>(std::lround(
        (forward / rangeWorld) * verticalSpan));
    projection.visible =
        projection.x >= geometry.rect.x &&
        projection.x < geometry.rect.x + geometry.rect.width &&
        projection.y >= geometry.topY && projection.y <= geometry.apexY;
    return projection;
}

std::string campaignCockpitRadarRangeLabel(uint16_t rangeMeters) {
    switch (rangeMeters) {
    case 4000u:
        return "4000 M";
    case 2000u:
        return "2000 M";
    case 1000u:
        return "1000 M";
    case 500u:
        return "500 M";
    default:
        return {};
    }
}

CampaignCockpitTargetMfdGeometry campaignCockpitTargetMfdGeometry(
    battle::CampaignCockpitFamily family) {
    if (family == battle::CampaignCockpitFamily::Heavy) {
        return {{24, 132, 53, 49}, 0};
    }
    return {{241, 123, 53, 49}, 0};
}

int campaignCockpitTargetScanSpriteIndex(std::string_view mechPresetId) {
    constexpr std::array<std::string_view, 8> kOriginalOrder{{
        "locust",
        "jenner",
        "phoenix_hawk",
        "shadow_hawk",
        "rifleman",
        "warhammer",
        "marauder",
        "battlemaster",
    }};
    const auto found = std::find(
        kOriginalOrder.begin(),
        kOriginalOrder.end(),
        mechPresetId);
    return found == kOriginalOrder.end()
        ? -1
        : static_cast<int>(std::distance(kOriginalOrder.begin(), found));
}

namespace {

struct TargetScanDamageRect {
    int x;
    int y;
    int width;
    int height;
};

// BTECH.EXE FUN_1000_6e44 indexes this original 8 chassis x 9 external-section
// table at DS:0x1AF8.  The executable stores screen coordinates; these values
// have the per-chassis origins at DS:0x1AD8 subtracted, exactly as the routine
// does before drawing into the 56x49 SM_MECHS frame.  Section 5 is rear CT and
// has no front-silhouette rectangle.  Later rectangles overwrite earlier
// overlaps, so HEAD (8) must retain last-match precedence.
constexpr std::array<std::array<TargetScanDamageRect, 9>, 8>
    kTargetScanDamageRects{{
        {{{38, 3, 3, 6}, {16, 3, 3, 6}, {31, 14, 7, 33},
          {19, 14, 7, 33}, {24, 3, 9, 15}, {0, 0, 0, 0},
          {34, 4, 3, 9}, {20, 4, 3, 9}, {26, 7, 5, 2}}},
        {{{39, 6, 7, 8}, {10, 6, 7, 8}, {31, 18, 6, 28},
          {19, 18, 6, 28}, {23, 4, 10, 17}, {0, 0, 0, 0},
          {33, 4, 5, 13}, {18, 4, 5, 13}, {23, 10, 10, 4}}},
        {{{37, 11, 9, 17}, {11, 11, 9, 18}, {30, 23, 8, 24},
          {19, 23, 8, 24}, {25, 6, 7, 20}, {0, 0, 0, 0},
          {32, 4, 5, 19}, {20, 4, 5, 19}, {26, 6, 5, 6}}},
        {{{38, 7, 6, 18}, {13, 7, 6, 29}, {28, 22, 10, 25},
          {19, 22, 9, 25}, {24, 6, 9, 20}, {0, 0, 0, 0},
          {31, 4, 7, 23}, {20, 6, 7, 20}, {26, 3, 5, 6}}},
        {{{36, 8, 8, 28}, {11, 8, 8, 28}, {29, 20, 11, 27},
          {15, 20, 11, 27}, {24, 3, 7, 23}, {0, 0, 0, 0},
          {31, 6, 5, 15}, {19, 6, 5, 15}, {25, 8, 5, 6}}},
        {{{38, 10, 4, 16}, {14, 10, 4, 16}, {28, 26, 12, 21},
          {16, 26, 12, 21}, {23, 12, 11, 15}, {0, 0, 0, 0},
          {33, 7, 3, 18}, {18, 7, 5, 18}, {23, 4, 10, 8}}},
        {{{36, 15, 8, 16}, {12, 15, 8, 16}, {30, 20, 8, 27},
          {18, 20, 8, 27}, {24, 4, 8, 17}, {0, 0, 0, 0},
          {32, 8, 4, 11}, {20, 8, 4, 11}, {25, 11, 6, 5}}},
        {{{38, 7, 6, 23}, {13, 7, 6, 34}, {30, 23, 7, 24},
          {20, 23, 7, 24}, {25, 12, 7, 14}, {0, 0, 0, 0},
          {32, 3, 5, 19}, {20, 9, 5, 13}, {26, 4, 5, 8}}},
    }};

constexpr std::array<mech3d::MechArmorSectionId, 9>
    kTargetScanExternalSections{{
        mech3d::MechArmorSectionId::LeftArm,
        mech3d::MechArmorSectionId::RightArm,
        mech3d::MechArmorSectionId::LeftLeg,
        mech3d::MechArmorSectionId::RightLeg,
        mech3d::MechArmorSectionId::CenterTorso,
        mech3d::MechArmorSectionId::CenterRear,
        mech3d::MechArmorSectionId::LeftTorso,
        mech3d::MechArmorSectionId::RightTorso,
        mech3d::MechArmorSectionId::Head,
    }};

} // namespace

std::optional<mech3d::MechArmorSectionId>
campaignCockpitTargetScanPixelArmorSection(
    int mechSpriteIndex,
    int sourceX,
    int sourceY) {
    if (mechSpriteIndex < 0 ||
        mechSpriteIndex >= static_cast<int>(kTargetScanDamageRects.size()) ||
        sourceX < 0 || sourceX >= 56 || sourceY < 0 || sourceY >= 49) {
        return std::nullopt;
    }
    std::optional<mech3d::MechArmorSectionId> result;
    const auto& rects =
        kTargetScanDamageRects[static_cast<size_t>(mechSpriteIndex)];
    // FUN_1000_6e44 fills the rectangles relative to the outer cockpit rect,
    // then blits SM_MECHS at outer + (1,1).  A sprite pixel therefore samples
    // the prepared damage fill one coordinate down and right.
    const int damageX = sourceX + 1;
    const int damageY = sourceY + 1;
    for (size_t sectionIndex = 0; sectionIndex < rects.size(); ++sectionIndex) {
        const TargetScanDamageRect& rect = rects[sectionIndex];
        if (rect.width > 0 && rect.height > 0 &&
            damageX >= rect.x && damageX < rect.x + rect.width &&
            damageY >= rect.y && damageY < rect.y + rect.height) {
            result = kTargetScanExternalSections[sectionIndex];
        }
    }
    return result;
}

battle::BattleMechSystemRole campaignCockpitTargetScanPixelRole(
    int mechSpriteIndex,
    int sourceX,
    int sourceY) {
    const auto section = campaignCockpitTargetScanPixelArmorSection(
        mechSpriteIndex,
        sourceX,
        sourceY);
    if (!section) {
        return battle::BattleMechSystemRole::Unknown;
    }
    switch (*section) {
    case mech3d::MechArmorSectionId::Head:
        return battle::BattleMechSystemRole::Cockpit;
    case mech3d::MechArmorSectionId::LeftArm:
    case mech3d::MechArmorSectionId::RightArm:
        return battle::BattleMechSystemRole::Weapons;
    case mech3d::MechArmorSectionId::LeftLeg:
    case mech3d::MechArmorSectionId::RightLeg:
        return battle::BattleMechSystemRole::Mobility;
    case mech3d::MechArmorSectionId::CenterTorso:
    case mech3d::MechArmorSectionId::CenterRear:
    case mech3d::MechArmorSectionId::LeftTorso:
    case mech3d::MechArmorSectionId::RightTorso:
        return battle::BattleMechSystemRole::Core;
    case mech3d::MechArmorSectionId::Count:
        break;
    }
    return battle::BattleMechSystemRole::Unknown;
}

battle::BattleMechSystemStatus campaignDetailedDamageDisplayStatus(
    uint8_t entryDamageLevel,
    int battleDamage,
    bool functional) {
    if (!functional || entryDamageLevel >= 3u) {
        return battle::BattleMechSystemStatus::Destroyed;
    }
    if (entryDamageLevel >= 2u) {
        return battle::BattleMechSystemStatus::Offline;
    }
    if (entryDamageLevel != 0u || battleDamage > 0) {
        return battle::BattleMechSystemStatus::Degraded;
    }
    return battle::BattleMechSystemStatus::Online;
}

battle::BattleMechSystemStatus campaignDetailedArmorDisplayStatus(
    const mech3d::MechDetailedDamageState& damage,
    mech3d::MechArmorSectionId sectionId) {
    if (!damage.valid || sectionId == mech3d::MechArmorSectionId::Count) {
        return battle::BattleMechSystemStatus::Online;
    }
    const auto internalId = [sectionId]() {
        switch (sectionId) {
        case mech3d::MechArmorSectionId::RightArm:
            return mech3d::MechInternalSectionId::RightArm;
        case mech3d::MechArmorSectionId::LeftArm:
            return mech3d::MechInternalSectionId::LeftArm;
        case mech3d::MechArmorSectionId::RightLeg:
            return mech3d::MechInternalSectionId::RightLeg;
        case mech3d::MechArmorSectionId::LeftLeg:
            return mech3d::MechInternalSectionId::LeftLeg;
        case mech3d::MechArmorSectionId::Head:
            return mech3d::MechInternalSectionId::Head;
        case mech3d::MechArmorSectionId::CenterTorso:
        case mech3d::MechArmorSectionId::CenterRear:
            return mech3d::MechInternalSectionId::CenterTorso;
        case mech3d::MechArmorSectionId::RightTorso:
            return mech3d::MechInternalSectionId::RightTorso;
        case mech3d::MechArmorSectionId::LeftTorso:
            return mech3d::MechInternalSectionId::LeftTorso;
        case mech3d::MechArmorSectionId::Count:
            break;
        }
        return mech3d::MechInternalSectionId::CenterTorso;
    }();
    const auto& armor = damage.armorSections[static_cast<size_t>(sectionId)];
    const auto& internal = damage.internalSections[static_cast<size_t>(internalId)];
    const int maximum = static_cast<int>(armor.armorMaximum) +
        static_cast<int>(internal.structureMaximum);
    const int remaining = static_cast<int>(armor.armorRemaining) +
        static_cast<int>(internal.structureRemaining);
    if (maximum <= 0 || remaining >= maximum) {
        return battle::BattleMechSystemStatus::Online;
    }
    if ((maximum >> 1) < remaining) {
        return battle::BattleMechSystemStatus::Degraded;
    }
    if (remaining == 0) {
        return battle::BattleMechSystemStatus::Destroyed;
    }
    return battle::BattleMechSystemStatus::Offline;
}

uint8_t campaignCockpitTargetScanDisplayColorIndex(
    uint8_t sourceColorIndex,
    battle::BattleMechSystemRole role,
    battle::BattleMechSystemStatus status,
    bool wholeMechDestroyed) {
    // SM_MECHS is a stencil, not a ready-to-copy color image.  Source 3 is
    // the black outline in the live scanner; source 0 exposes the gray or
    // damage-colored body fill prepared behind it.  Background/arrows retain
    // their source EGA indices.
    if (sourceColorIndex == 3u) {
        return 0u;
    }
    if (sourceColorIndex != 0u || role == battle::BattleMechSystemRole::Unknown) {
        return sourceColorIndex;
    }
    if (wholeMechDestroyed) {
        return 0u;
    }
    switch (status) {
    case battle::BattleMechSystemStatus::Degraded:
        return 14u;
    case battle::BattleMechSystemStatus::Offline:
        return 4u;
    case battle::BattleMechSystemStatus::Destroyed:
        return 0u;
    case battle::BattleMechSystemStatus::Online:
    default:
        return 7u;
    }
}

uint32_t campaignBattleMapPlayerBlipBgra(
    battle::CampaignBattlePresentationMode mode,
    std::optional<int> environmentId) {
    if (mode == battle::CampaignBattlePresentationMode::CockpitCommandMap &&
        environmentId == 2) {
        return 0xff000000u;
    }
    return 0xffffffffu;
}

int campaignBattlePlayerLanceSlot(
    std::string_view rosterSourceSlot,
    bool playerControlled) {
    constexpr std::array<std::string_view, 4> kPlayerSlots{{
        "player:0", "player:1", "player:2", "player:3",
    }};
    for (size_t slot = 0; slot < kPlayerSlots.size(); ++slot) {
        if (rosterSourceSlot == kPlayerSlots[slot]) {
            return static_cast<int>(slot);
        }
    }
    return playerControlled ? 0 : -1;
}

CampaignBattleMapPlayerMarker campaignBattleMapPlayerMarker(
    int playerLanceSlot,
    battle::CampaignBattlePresentationMode mode,
    std::optional<int> environmentId) {
    constexpr std::array<uint8_t, 9> kSquare{{
        1, 1, 1,
        1, 1, 1,
        1, 1, 1,
    }};
    constexpr std::array<uint8_t, 9> kH{{
        1, 0, 1,
        1, 1, 1,
        1, 0, 1,
    }};
    constexpr std::array<uint8_t, 9> kRotatedH{{
        1, 1, 1,
        0, 1, 0,
        1, 1, 1,
    }};
    constexpr std::array<uint8_t, 9> kPlus{{
        0, 1, 0,
        1, 1, 1,
        0, 1, 0,
    }};
    constexpr std::array<uint8_t, 9> kDot{{
        0, 0, 0,
        0, 1, 0,
        0, 0, 0,
    }};
    switch (playerLanceSlot) {
    case 0:
        return {
            CampaignBattleMapPlayerMarkerShape::Square,
            campaignBattleMapPlayerBlipBgra(mode, environmentId),
            static_cast<uint8_t>(
                mode == battle::CampaignBattlePresentationMode::CockpitCommandMap &&
                        environmentId == 2
                    ? 0u
                    : 15u),
            kSquare,
        };
    case 1:
        return {
            CampaignBattleMapPlayerMarkerShape::H,
            0xff5555ffu,
            9u,
            kH,
        };
    case 2:
        return {
            CampaignBattleMapPlayerMarkerShape::RotatedH,
            0xff55ffffu,
            11u,
            kRotatedH,
        };
    case 3:
        return {
            CampaignBattleMapPlayerMarkerShape::Plus,
            0xff00aa00u,
            2u,
            kPlus,
            environmentId == 1 &&
                (mode == battle::CampaignBattlePresentationMode::MissionStatus ||
                 mode == battle::CampaignBattlePresentationMode::CockpitCommandMap ||
                 mode == battle::CampaignBattlePresentationMode::TacticalMap),
        };
    default:
        return {
            CampaignBattleMapPlayerMarkerShape::Dot,
            0xffffffffu,
            15u,
            kDot,
        };
    }
}

uint32_t campaignCockpitMinimapPlayerBlipBgra(bool playerControlled) {
    return playerControlled ? 0xff000000u : 0xffffffffu;
}

CampaignCockpitBackdropImage loadCampaignCockpitBackdrop(
    const battle::CampaignBattleRenderCockpitResources& resources) {
    CampaignCockpitBackdropImage image;
    image.sourcePath = resources.backdropPath;
    image.palettePath = resources.palettePath;
    if (!resources.valid) {
        image.reason = "campaign_cockpit_resources_invalid";
        return image;
    }
    try {
        const std::vector<uint8_t> data = loadBytes(resources.backdropPath, "SCR");
        size_t offset = 0;
        size_t size = 0;
        if (!findTaggedPayload(data, "BIN:", 0u, data.size(), offset, size)) {
            throw std::runtime_error("cockpit SCR has no BIN chunk: " + resources.backdropPath.string());
        }
        const std::vector<uint8_t> payload(
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        const std::vector<uint8_t> decoded = decodeBin(payload, image.codec);
        constexpr size_t packedSize = static_cast<size_t>(kScreenWidth / 2) * kScreenHeight;
        if (decoded.size() != packedSize) {
            throw std::runtime_error("cockpit SCR is not packed 320x200 4bpp");
        }

        const auto palette = loadPalette(resources.palettePath);
        image.width = kScreenWidth;
        image.height = kScreenHeight;
        image.bgraPixels.resize(static_cast<size_t>(kScreenWidth) * kScreenHeight);
        for (size_t pixel = 0; pixel < image.bgraPixels.size(); ++pixel) {
            const uint8_t packed = decoded[pixel / 2u];
            const uint8_t color = (pixel % 2u == 0u)
                                      ? static_cast<uint8_t>((packed >> 4u) & 0x0fu)
                                      : static_cast<uint8_t>(packed & 0x0fu);
            const auto& rgb = palette[color];
            image.bgraPixels[pixel] = 0xff000000u |
                                      (static_cast<uint32_t>(rgb[0]) << 16u) |
                                      (static_cast<uint32_t>(rgb[1]) << 8u) |
                                      static_cast<uint32_t>(rgb[2]);
        }
        image.valid = true;
    } catch (const std::exception& error) {
        image.reason = error.what();
    }
    return image;
}

bool CampaignCockpitFont::contains(char ch) const {
    const int code = static_cast<unsigned char>(ch);
    return code >= firstCode && code < firstCode + glyphCount;
}

CampaignIndexedSpriteArchive loadCampaignIndexedSpriteArchive(
    const std::filesystem::path& path) {
    CampaignIndexedSpriteArchive archive;
    try {
        archive.sprites = loadIndexedSprites(path);
        archive.valid = !archive.sprites.empty();
        if (!archive.valid) {
            archive.reason = "campaign_indexed_sprite_archive_empty";
        }
    } catch (const std::exception& error) {
        archive.reason = error.what();
    }
    return archive;
}

CampaignCockpitDynamicLayers loadCampaignCockpitDynamicLayers(
    const battle::CampaignBattleRenderCockpitResources& resources) {
    CampaignCockpitDynamicLayers layers;
    if (!resources.valid) {
        layers.reason = "campaign_cockpit_resources_invalid";
        return layers;
    }
    try {
        layers.struts = loadSprites(resources.strutsPath, resources.palettePath);
        layers.widgets = loadSprites(resources.widgetsPath, resources.palettePath);
        layers.hudNumbers = loadSprites(resources.hudNumbersPath, resources.palettePath);
        layers.font = loadFont(resources.hudFontPath);
        layers.valid =
            !layers.struts.empty() &&
            !layers.widgets.empty() &&
            !layers.hudNumbers.empty() &&
            !layers.font.rows.empty();
        if (!layers.valid) {
            layers.reason = "campaign_cockpit_dynamic_layers_incomplete";
        }
    } catch (const std::exception& error) {
        layers.reason = error.what();
    }
    return layers;
}

std::vector<CampaignCockpitSpritePlacement> campaignCockpitStrutPlacements(
    battle::CampaignCockpitFamily family) {
    const CampaignCockpitRect viewport = campaignCockpitLayout(family).viewport;
    const int centerX = viewport.x + viewport.width / 2;
    if (family == battle::CampaignCockpitFamily::Heavy) {
        return {{9, viewport.x, viewport.y},
                {10, viewport.x + viewport.width - 32, viewport.y},
                {11, centerX - 55, viewport.y + viewport.height - 3}};
    }
    if (family == battle::CampaignCockpitFamily::Medium) {
        return {{5, viewport.x - 1, viewport.y},
                {7, viewport.x + viewport.width - 14, viewport.y},
                {6, viewport.x - 1, viewport.y + viewport.height - 14},
                {8, viewport.x + viewport.width - 14, viewport.y + viewport.height - 14},
                {11, centerX - 55, viewport.y + viewport.height - 3}};
    }
    return {{0, viewport.x - 3, viewport.y},
            {1, viewport.x + viewport.width - 20, viewport.y},
            {2, viewport.x - 3, viewport.y + viewport.height - 30},
            {3, viewport.x + viewport.width - 36, viewport.y + viewport.height - 30},
            {4, centerX - 37, viewport.y},
            {11, centerX - 55, viewport.y + viewport.height - 3}};
}

size_t campaignCockpitHudColorCount() {
    return std::size(kHudColors);
}

const CampaignCockpitHudColor& campaignCockpitHudColor(size_t index) {
    return kHudColors[index % std::size(kHudColors)];
}

CampaignCockpitGaugeState campaignCockpitGaugeState(
    const battle::CombatantSnapshot& combatant,
    battle::CampaignCockpitFamily family) {
    CampaignCockpitGaugeState state;
    state.speedX = family == battle::CampaignCockpitFamily::Heavy
                       ? kHeavySpeedGaugeX
                       : kSpeedGaugeX;
    state.speedY = family == battle::CampaignCockpitFamily::Heavy
                       ? kHeavySpeedGaugeY
                       : kSpeedGaugeY;

    const double scaleSpeed = std::max(1.0, combatant.maxForwardSpeed);
    const int filledBars = static_cast<int>(std::floor(
        std::clamp(std::abs(combatant.forwardSpeed) / scaleSpeed, 0.0, 1.0) *
            kSpeedGaugeMaxForwardBars +
        0.000001));
    if (combatant.forwardSpeed > 0.0001) {
        state.forwardBars = std::min(
            kSpeedGaugeBarCount - kSpeedGaugeZeroIndex - 1,
            filledBars);
    } else if (combatant.forwardSpeed < -0.0001) {
        state.reverseBars = std::min(kSpeedGaugeZeroIndex, filledBars);
    }

    state.heatX = kHeatGaugeX;
    state.heatBottomY = kHeatGaugeBottomY;
    state.heatBars = std::clamp(
        combatant.heat.rawHeat / battle::battleRawHeatPerGaugeSegment(),
        0,
        battle::battleHeatGaugeSegmentCount());

    state.jumpGaugeVisible = family != battle::CampaignCockpitFamily::Heavy;
    const battle::CampaignCockpitFamily anchorFamily =
        family == battle::CampaignCockpitFamily::Medium
            ? battle::CampaignCockpitFamily::Light
            : family;
    const CampaignCockpitRect& panel = campaignCockpitLayout(anchorFamily).rightPanel;
    state.jumpGauge = {
        panel.x + panel.width - kJumpGaugeRightInset,
        panel.y + kJumpGaugeTopInset,
        kJumpGaugeWidth,
        kJumpGaugeHeight,
    };
    state.jumpReadyLight = {
        panel.x + panel.width - kJumpReadyLightRightInset + 12,
        panel.y + kJumpReadyLightTopInset + 3,
        3,
        3,
    };
    const double capacity = std::max(0.001, combatant.jumpMaxFuel);
    state.jumpFuelFillHeight = std::clamp(
        static_cast<int>(std::lround(
            std::clamp(combatant.jumpFuel / capacity, 0.0, 1.0) * kJumpGaugeHeight)),
        0,
        kJumpGaugeHeight);
    state.jumpActivationThresholdY =
        state.jumpGauge.y + kJumpGaugeHeight -
        std::clamp(
            static_cast<int>(std::lround(
                std::clamp(combatant.jumpActivationFuel / capacity, 0.0, 1.0) *
                kJumpGaugeHeight)),
            0,
            kJumpGaugeHeight);
    return state;
}

CampaignCockpitViewportHudLayout campaignCockpitViewportHudLayout(
    battle::CampaignCockpitFamily family,
    int aimPitchStep) {
    const CampaignCockpitLayout& layout = campaignCockpitLayout(family);
    CampaignCockpitViewportHudLayout state;
    state.crosshairCenterX = layout.viewport.x + layout.viewport.width / 2;
    const int neutralCenterY =
        family == battle::CampaignCockpitFamily::Light ? 62 : 51;
    state.crosshairCenterY = neutralCenterY - aimPitchStep * 3;
    state.leftLabelX =
        layout.viewport.x + std::max(18, layout.viewport.width / 6);
    state.rightLabelX =
        layout.viewport.x + layout.viewport.width -
        (family == battle::CampaignCockpitFamily::Heavy
             ? 87
             : std::max(72, layout.viewport.width / 4)) -
        12;
    state.labelY = layout.viewport.y + layout.viewport.height - 7;
    state.zoomLabelOnLeft = family == battle::CampaignCockpitFamily::Heavy;
    return state;
}

CampaignCockpitMinimapTerrain loadCampaignCockpitMinimapTerrain(
    const battle::CampaignBattleRenderTerrainResources& terrain) {
    CampaignCockpitMinimapTerrain result;
    try {
        if (terrain.worldPaths.size() != 4u || terrain.terrainShapePath.empty()) {
            throw std::runtime_error(
                "campaign cockpit minimap requires TERPCK and four WLD tiles");
        }
        const double spanX = terrain.boundsMaxX - terrain.boundsMinX;
        const double spanZ = terrain.boundsMaxZ - terrain.boundsMinZ;
        const double cellX = spanX / 173.0;
        const double cellZ = spanZ / 93.0;
        const float cellSize = static_cast<float>(
            std::max(1.0, (cellX + cellZ) * 0.5));
        const legacy3d::TerrainPlacementBounds placementBounds{
            static_cast<float>(terrain.boundsMinX),
            static_cast<float>(terrain.boundsMaxX),
            static_cast<float>(terrain.boundsMinZ),
            static_cast<float>(terrain.boundsMaxZ),
        };
        const std::vector<legacy3d::RuntimeRecord> records =
            legacy3d::loadRuntimeShapeRecords(terrain.terrainShapePath);
        legacy3d::GpuBatchOptions options;
        options.shadeMode = "stored";
        options.swizzle = "xzy";
        constexpr double mapScale = campaignBattleMapWorldUnitsPerPixel();
        std::map<std::pair<int, int>, uint8_t> rasterSamples;
        const auto interpolatedTriangleHeight = [](
                                                    double px, double pz,
                                                    double ax, double ay, double az,
                                                    double bx, double by, double bz,
                                                    double cx, double cy, double cz)
            -> std::optional<double> {
            const double denominator =
                (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (std::abs(denominator) <= 0.001) {
                return std::nullopt;
            }
            const double weightA =
                ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) /
                denominator;
            const double weightB =
                ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) /
                denominator;
            const double weightC = 1.0 - weightA - weightB;
            constexpr double kEdgeTolerance = 0.001;
            if (weightA < -kEdgeTolerance || weightB < -kEdgeTolerance ||
                weightC < -kEdgeTolerance) {
                return std::nullopt;
            }
            return weightA * ay + weightB * by + weightC * cy;
        };
        const auto heightColorBand = [](double localHeight, double totalHeight) {
            const double normalizedHeight =
                totalHeight > 0.001
                    ? std::clamp(localHeight / totalHeight, 0.0, 1.0)
                    : 0.0;
            return normalizedHeight < 0.25
                       ? uint8_t{0}
                       : normalizedHeight < 0.50
                             ? uint8_t{1}
                             : normalizedHeight < 0.75 ? uint8_t{2} : uint8_t{3};
        };

        for (const std::filesystem::path& worldPath : terrain.worldPaths) {
            const legacy3d::TerrainWorld world = legacy3d::loadTerrainWorld(worldPath);
            const std::vector<legacy3d::TerrainObjectPlacement> placements =
                legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(
                    world, placementBounds, cellSize);
            for (const legacy3d::TerrainObjectPlacement& placement : placements) {
                if (static_cast<size_t>(placement.recordIndex) >= records.size()) {
                    throw std::runtime_error(
                        "campaign cockpit minimap TERPCK record is out of range");
                }
                const legacy3d::GpuBatch batch = legacy3d::buildGpuBatch(
                    records[static_cast<size_t>(placement.recordIndex)], options);
                if (batch.vertices.empty()) {
                    continue;
                }
                double minX = batch.vertices[0], maxX = batch.vertices[0];
                double minY = batch.vertices[1], maxY = batch.vertices[1];
                double minZ = batch.vertices[2], maxZ = batch.vertices[2];
                for (size_t offset = 0; offset + 5u < batch.vertices.size(); offset += 6u) {
                    minX = std::min(minX, static_cast<double>(batch.vertices[offset]));
                    maxX = std::max(maxX, static_cast<double>(batch.vertices[offset]));
                    minY = std::min(minY, static_cast<double>(batch.vertices[offset + 1u]));
                    maxY = std::max(maxY, static_cast<double>(batch.vertices[offset + 1u]));
                    minZ = std::min(minZ, static_cast<double>(batch.vertices[offset + 2u]));
                    maxZ = std::max(maxZ, static_cast<double>(batch.vertices[offset + 2u]));
                }
                const double centerX = (minX + maxX) * 0.5;
                const double centerZ = (minZ + maxZ) * 0.5;
                const double height = maxY - minY;
                if (height > 1.0 && height <= 300.0) {
                    ++result.driveableSurfacePlacements;
                } else if (height > 300.0) {
                    ++result.solidMountainPlacements;
                }
                const bool driveableSurface = height <= 300.0;
                const auto worldVertex = [&](uint32_t vertexIndex) {
                    const size_t offset = static_cast<size_t>(vertexIndex) * 6u;
                    return std::array<double, 3>{
                        batch.vertices[offset] + placement.x - centerX,
                        batch.vertices[offset + 1u],
                        placement.z + centerZ - batch.vertices[offset + 2u],
                    };
                };
                std::array<bool, 4> placementHeightBands{};
                for (size_t index = 0; index + 2u < batch.triangleIndices.size(); index += 3u) {
                    const auto [ax, ay, az] = worldVertex(batch.triangleIndices[index]);
                    const auto [bx, by, bz] = worldVertex(batch.triangleIndices[index + 1u]);
                    const auto [cx, cy, cz] = worldVertex(batch.triangleIndices[index + 2u]);
                    const int minGridX = static_cast<int>(
                        std::floor(std::min({ax, bx, cx}) / mapScale));
                    const int maxGridX = static_cast<int>(
                        std::ceil(std::max({ax, bx, cx}) / mapScale));
                    const int minGridZ = static_cast<int>(
                        std::floor(std::min({az, bz, cz}) / mapScale));
                    const int maxGridZ = static_cast<int>(
                        std::ceil(std::max({az, bz, cz}) / mapScale));
                    for (int gridX = minGridX; gridX <= maxGridX; ++gridX) {
                        for (int gridZ = minGridZ; gridZ <= maxGridZ; ++gridZ) {
                            const double worldX = static_cast<double>(gridX) * mapScale;
                            const double worldZ = static_cast<double>(gridZ) * mapScale;
                            const std::optional<double> sampleHeight =
                                interpolatedTriangleHeight(
                                    worldX, worldZ,
                                    ax, ay, az, bx, by, bz, cx, cy, cz);
                            if (!sampleHeight.has_value()) {
                                continue;
                            }
                            // Records 26..29 are the proven low, driveable slope
                            // domain. Until pitch sampling is implemented they stay
                            // a single green map band. Raised terrain instead uses
                            // the actual triangle height at this map sample, matching
                            // the original MFD's visible elevation bands.
                            const uint8_t colorBand = driveableSurface
                                                          ? uint8_t{0}
                                                          : heightColorBand(
                                                                std::clamp(
                                                                    *sampleHeight - minY,
                                                                    0.0,
                                                                    height),
                                                                height);
                            placementHeightBands[colorBand] = true;
                            uint8_t& sample = rasterSamples[{gridX, gridZ}];
                            sample = std::max(sample, colorBand);
                        }
                    }
                }
                if (!driveableSurface) {
                    const int placementBandCount = static_cast<int>(std::count(
                        placementHeightBands.begin(),
                        placementHeightBands.end(),
                        true));
                    if (placementBandCount >= 2) {
                        ++result.solidMountainGradientPlacements;
                    }
                    if (placementBandCount == 4) {
                        ++result.solidMountainFourBandPlacements;
                    }
                }
            }
        }
        result.samples.reserve(rasterSamples.size());
        for (const auto& [coordinate, colorBand] : rasterSamples) {
            ++result.heightBandSampleCounts[
                std::min<size_t>(colorBand, result.heightBandSampleCounts.size() - 1u)];
            result.samples.push_back({
                static_cast<double>(coordinate.first) * mapScale,
                static_cast<double>(coordinate.second) * mapScale,
                colorBand,
            });
        }
        result.provenance =
            "WLD_TERPCK_projected_triangle_raster_surface_flat_mountain_height_bands";
        result.valid = true;
    } catch (const std::exception& error) {
        result.reason = error.what();
    }
    return result;
}

} // namespace mw::presentation
