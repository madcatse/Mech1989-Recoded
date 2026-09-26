#include "shape_parser.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace mw::legacy3d {
namespace {

uint8_t u8(const std::vector<uint8_t>& buf, size_t off) {
    if (off >= buf.size()) {
        throw ParseError("u8 read out of range");
    }
    return buf[off];
}

uint16_t u16le(const std::vector<uint8_t>& buf, size_t off) {
    if (off + 1 >= buf.size()) {
        throw ParseError("u16 read out of range");
    }
    return static_cast<uint16_t>(buf[off] | (static_cast<uint16_t>(buf[off + 1]) << 8));
}

int16_t s16le(const std::vector<uint8_t>& buf, size_t off) {
    return static_cast<int16_t>(u16le(buf, off));
}

uint32_t u32le(const std::vector<uint8_t>& buf, size_t off) {
    if (off + 3 >= buf.size()) {
        throw ParseError("u32 read out of range");
    }
    return static_cast<uint32_t>(buf[off]) |
           (static_cast<uint32_t>(buf[off + 1]) << 8) |
           (static_cast<uint32_t>(buf[off + 2]) << 16) |
           (static_cast<uint32_t>(buf[off + 3]) << 24);
}

void requireRange(const std::vector<uint8_t>& buf, size_t off, size_t len, const char* what) {
    if (off > buf.size() || len > buf.size() - off) {
        std::ostringstream oss;
        oss << what << " out of range at 0x" << std::hex << off;
        throw ParseError(oss.str());
    }
}

class LsbBitReader {
public:
    explicit LsbBitReader(const std::vector<uint8_t>& payload) : payload_(payload) {}

    uint16_t readBits(int nbits) {
        uint16_t value = 0;
        for (int outBit = 0; outBit < nbits; ++outBit) {
            if (bit_ == 8) {
                if (pos_ >= payload_.size()) {
                    throw ParseError("compressed stream ended early");
                }
                current_ = payload_[pos_++];
                bit_ = 0;
            }
            if ((current_ & (1u << bit_)) != 0) {
                value = static_cast<uint16_t>(value | (1u << outBit));
            }
            ++bit_;
        }
        return value;
    }

private:
    const std::vector<uint8_t>& payload_;
    size_t pos_ = 0;
    uint8_t current_ = 0;
    int bit_ = 8;
};

std::vector<uint8_t> unpackRleDynamix(const std::vector<uint8_t>& payload, size_t expectedSize) {
    std::vector<uint8_t> out;
    out.reserve(expectedSize);
    size_t pos = 0;
    while (pos < payload.size() && out.size() < expectedSize) {
        const uint8_t control = payload[pos++];
        if ((control & 0x80u) != 0) {
            if (pos >= payload.size()) {
                throw ParseError("RLE repeat command lacks count byte");
            }
            const uint8_t count = payload[pos++];
            out.insert(out.end(), count, static_cast<uint8_t>(control & 0x7Fu));
        } else {
            const size_t count = control;
            if (pos + count > payload.size()) {
                throw ParseError("RLE literal run exceeds payload");
            }
            out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(pos), payload.begin() + static_cast<std::ptrdiff_t>(pos + count));
            pos += count;
        }
    }
    if (out.size() < expectedSize) {
        throw ParseError("RLE output too short");
    }
    out.resize(expectedSize);
    return out;
}

std::vector<uint8_t> unpackLzwDynamix(const std::vector<uint8_t>& payload, size_t expectedSize) {
    struct Entry {
        uint16_t prefix = 0;
        uint8_t appended = 0;
        bool valid = false;
    };

    LsbBitReader reader(payload);
    std::array<Entry, 4096> table{};
    std::vector<uint8_t> out;
    std::vector<uint8_t> stack;
    out.reserve(expectedSize);
    stack.reserve(4096);

    int nbits = 9;
    uint16_t freeCode = 257;
    int oldCode = -1;
    uint8_t lastByte = 0;
    int codesAtWidth = 0;

    auto readCode = [&]() -> uint16_t {
        const uint16_t code = reader.readBits(nbits);
        ++codesAtWidth;
        return code;
    };

    auto resetDictionary = [&]() {
        while ((codesAtWidth % 8) != 0) {
            try {
                (void)readCode();
            } catch (const ParseError&) {
                break;
            }
        }
        nbits = 9;
        freeCode = 257;
        oldCode = -1;
        lastByte = 0;
        codesAtWidth = 0;
    };

    while (out.size() < expectedSize) {
        const uint16_t code = readCode();
        if (code == 256) {
            resetDictionary();
            continue;
        }
        if (oldCode < 0) {
            if (code > 255) {
                throw ParseError("first LZW code after reset/start is not a literal");
            }
            out.push_back(static_cast<uint8_t>(code));
            oldCode = code;
            lastByte = static_cast<uint8_t>(code);
            continue;
        }

        uint16_t decodeCode = code;
        if (decodeCode >= freeCode) {
            stack.push_back(lastByte);
            decodeCode = static_cast<uint16_t>(oldCode);
        }

        int guard = 0;
        while (decodeCode >= 256) {
            const Entry& entry = table[decodeCode];
            if (!entry.valid) {
                throw ParseError("missing LZW dictionary entry");
            }
            stack.push_back(entry.appended);
            decodeCode = entry.prefix;
            if (++guard > 4096) {
                throw ParseError("LZW dictionary cycle detected");
            }
        }

        stack.push_back(static_cast<uint8_t>(decodeCode & 0xFFu));
        lastByte = static_cast<uint8_t>(decodeCode & 0xFFu);
        while (!stack.empty() && out.size() < expectedSize) {
            out.push_back(stack.back());
            stack.pop_back();
        }
        stack.clear();

        if (freeCode < table.size()) {
            table[freeCode] = Entry{static_cast<uint16_t>(oldCode), lastByte, true};
            ++freeCode;
            if (freeCode >= (1u << nbits) && nbits < 12) {
                ++nbits;
                codesAtWidth = 0;
            }
        }

        oldCode = code;
    }

    return out;
}

std::string classifyPrimitive(uint8_t opcode, const std::vector<uint8_t>& indices) {
    if (opcode == 0x80 || indices.size() == 2) {
        return "line";
    }
    if (opcode == 0x81 && indices.size() >= 3) {
        return "polygon";
    }
    if (indices.size() >= 3) {
        return "polygon_candidate";
    }
    return "degenerate";
}

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3f sub(Vec3f a, Vec3f b) {
    return Vec3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f cross(Vec3f a, Vec3f b) {
    return Vec3f{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f normalize(Vec3f v) {
    const float d = std::sqrt(dot(v, v));
    if (d <= 1.0e-9f) {
        return Vec3f{0.0f, 0.0f, 1.0f};
    }
    return Vec3f{v.x / d, v.y / d, v.z / d};
}

Vec3f transformVertex(Vec3i v, float scale, const std::string& swizzle) {
    const float x = static_cast<float>(v.x) * scale;
    const float y = static_cast<float>(v.y) * scale;
    const float z = static_cast<float>(v.z) * scale;
    if (swizzle == "xyz") {
        return Vec3f{x, y, z};
    }
    if (swizzle == "xzy") {
        return Vec3f{x, z, y};
    }
    if (swizzle == "yxz") {
        return Vec3f{y, x, z};
    }
    if (swizzle == "yzx") {
        return Vec3f{y, z, x};
    }
    if (swizzle == "zxy") {
        return Vec3f{z, x, y};
    }
    if (swizzle == "zyx") {
        return Vec3f{z, y, x};
    }
    throw ParseError("unsupported vertex swizzle: " + swizzle);
}

std::array<float, 3> egaRgb(uint8_t color) {
    switch (color & 0x0fu) {
    case 0:
        return {0.0f, 0.0f, 0.0f};
    case 1:
        return {0.0f, 0.0f, 170.0f / 255.0f};
    case 2:
        return {0.0f, 170.0f / 255.0f, 0.0f};
    case 3:
        return {0.0f, 170.0f / 255.0f, 170.0f / 255.0f};
    case 4:
        return {170.0f / 255.0f, 0.0f, 0.0f};
    case 5:
        return {170.0f / 255.0f, 0.0f, 170.0f / 255.0f};
    case 6:
        return {170.0f / 255.0f, 85.0f / 255.0f, 0.0f};
    case 7:
        return {170.0f / 255.0f, 170.0f / 255.0f, 170.0f / 255.0f};
    case 8:
        return {85.0f / 255.0f, 85.0f / 255.0f, 85.0f / 255.0f};
    case 9:
        return {85.0f / 255.0f, 85.0f / 255.0f, 1.0f};
    case 10:
        return {85.0f / 255.0f, 1.0f, 85.0f / 255.0f};
    case 11:
        return {85.0f / 255.0f, 1.0f, 1.0f};
    case 12:
        return {1.0f, 85.0f / 255.0f, 85.0f / 255.0f};
    case 13:
        return {1.0f, 85.0f / 255.0f, 1.0f};
    case 14:
        return {1.0f, 1.0f, 85.0f / 255.0f};
    default:
        return {1.0f, 1.0f, 1.0f};
    }
}

std::array<float, 3> primitiveRgb(const RuntimePrimitive& prim, Vec3f normal, const std::string& shadeMode) {
    if (shadeMode == "ega") {
        const int shade = ((static_cast<int>(prim.shadeBytes[0]) +
                            static_cast<int>(prim.shadeBytes[1]) +
                            static_cast<int>(prim.shadeBytes[2]) +
                            static_cast<int>(prim.shadeBytes[3]) +
                            2) /
                           4) &
                          0x0f;
        return egaRgb(static_cast<uint8_t>(shade));
    }
    if (shadeMode == "stored") {
        const double shadeLevel = (static_cast<int>(prim.shadeBytes[0]) +
                                   static_cast<int>(prim.shadeBytes[1]) +
                                   static_cast<int>(prim.shadeBytes[2]) +
                                   static_cast<int>(prim.shadeBytes[3])) /
                                  4.0;
        int level = 58 + static_cast<int>(std::max(0.0, std::min(15.0, shadeLevel)) * 11.5);
        if ((prim.materialOrFlags & 0x80u) != 0) {
            level += 10;
        }
        const float v = static_cast<float>(std::max(0, std::min(255, level)) / 255.0);
        return {v, v, v};
    }

    const Vec3f light = normalize(Vec3f{0.35f, 0.65f, -0.95f});
    const float lit = std::max(std::abs(dot(normal, light)), 0.18f);
    const int groupBias = (prim.partIndex % 4) * 12;
    const int level = 78 + static_cast<int>(lit * 132.0f) + groupBias;
    const float v = static_cast<float>(std::max(0, std::min(255, level)) / 255.0);
    return {v, v, v};
}

std::vector<int> validIndices(const RuntimeRecord& record, const RuntimePrimitive& prim, const std::string& indexMode) {
    std::vector<int> valid;
    valid.reserve(prim.normalizedIndices.size());
    for (uint8_t rawIndex : prim.normalizedIndices) {
        const int idx = indexMode == "minus1" ? static_cast<int>(rawIndex) - 1 : static_cast<int>(rawIndex);
        if (idx >= 0 && static_cast<size_t>(idx) < record.vertices.size()) {
            valid.push_back(idx);
        }
    }
    return valid;
}

std::vector<uint8_t> normalizeIndices(const std::vector<uint8_t>& raw) {
    if (raw.size() >= 2 && raw[0] == raw[1]) {
        return std::vector<uint8_t>(raw.begin() + 1, raw.end());
    }
    return raw;
}

std::vector<RuntimePrimitive> parseType0Primitives(
    const std::vector<uint8_t>& buf,
    uint32_t segbase,
    uint32_t cmdAbs,
    int partIndex,
    int commandIndex
) {
    requireRange(buf, cmdAbs, 8, "type-0 command");
    const uint16_t primitiveCount = u16le(buf, cmdAbs + 2);
    const uint16_t descPtr = u16le(buf, cmdAbs + 4);
    const uint32_t descAbs = segbase + descPtr;
    requireRange(buf, descAbs, static_cast<size_t>(primitiveCount) * 8, "primitive descriptor table");

    std::vector<RuntimePrimitive> out;
    out.reserve(primitiveCount);
    for (uint16_t pi = 0; pi < primitiveCount; ++pi) {
        const uint32_t d = descAbs + static_cast<uint32_t>(pi) * 8u;
        RuntimePrimitive prim;
        prim.partIndex = partIndex;
        prim.commandIndex = commandIndex;
        prim.primitiveIndex = pi;
        prim.descriptorOffset = static_cast<uint16_t>(d - segbase);
        prim.opcode = u8(buf, d);
        for (size_t i = 0; i < 4; ++i) {
            prim.shadeBytes[i] = u8(buf, d + 1 + i);
        }
        prim.materialOrFlags = u8(buf, d + 5);
        prim.indexListOffset = u16le(buf, d + 6);

        uint32_t q = segbase + prim.indexListOffset;
        requireRange(buf, q, 1, "primitive index list");
        while (q < buf.size() && buf[q] != 0xFF) {
            prim.rawIndices.push_back(buf[q]);
            ++q;
        }
        prim.normalizedIndices = normalizeIndices(prim.rawIndices);
        prim.kind = classifyPrimitive(prim.opcode, prim.normalizedIndices);
        out.push_back(std::move(prim));
    }
    return out;
}

} // namespace

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ParseError("could not open file: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        throw ParseError("could not determine file size: " + path.string());
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in) {
            throw ParseError("could not read whole file: " + path.string());
        }
    }
    return data;
}

std::vector<uint8_t> unpackDynamixBlock(const std::vector<uint8_t>& input) {
    if (input.size() < 5) {
        throw ParseError("input too short");
    }
    const uint8_t ctype = input[0];
    const uint32_t expectedSize = u32le(input, 1);
    if ((ctype != 0 && ctype != 1 && ctype != 2) || expectedSize == 0 || expectedSize > 64u * 1024u * 1024u) {
        return input;
    }
    std::vector<uint8_t> payload(input.begin() + 5, input.end());
    if (ctype == 0) {
        if (payload.size() < expectedSize) {
            throw ParseError("stored Dynamix payload shorter than declared size");
        }
        payload.resize(expectedSize);
        return payload;
    }
    if (ctype == 1) {
        return unpackRleDynamix(payload, expectedSize);
    }
    return unpackLzwDynamix(payload, expectedSize);
}

std::vector<PointerEntry> parsePointerTable(const std::vector<uint8_t>& buf) {
    std::vector<PointerEntry> entries;
    for (size_t off = 0; off + 3 < buf.size(); off += 4) {
        PointerEntry entry;
        entry.offset = u16le(buf, off);
        entry.segment = u16le(buf, off + 2);
        entry.linear = static_cast<uint32_t>(entry.segment) * 16u + entry.offset;
        entries.push_back(entry);
        if (entry.offset == 0 && entry.segment == 0) {
            break;
        }
    }
    if (entries.empty()) {
        throw ParseError("empty pointer table");
    }
    return entries;
}

std::vector<RuntimeRecord> loadRuntimeShapeRecords(const std::filesystem::path& path) {
    const std::vector<uint8_t> src = readFileBytes(path);
    const std::vector<uint8_t> buf = unpackDynamixBlock(src);
    const std::vector<PointerEntry> pointers = parsePointerTable(buf);
    const std::string resourceName = path.stem().string();

    std::vector<RuntimeRecord> records;
    records.reserve(pointers.size());
    for (size_t idx = 0; idx < pointers.size(); ++idx) {
        const PointerEntry& entry = pointers[idx];
        if (entry.offset == 0 && entry.segment == 0) {
            continue;
        }
        requireRange(buf, entry.linear, 14, "record prefix");
        RuntimeRecord record;
        record.resourceName = resourceName;
        record.recordIndex = static_cast<int>(idx);
        record.recordOffset = entry.offset;
        record.recordSegment = entry.segment;
        record.segmentBase = static_cast<uint32_t>(entry.segment) * 16u;
        record.flags = u8(buf, entry.linear);
        record.scaleShift = u8(buf, entry.linear + 1);
        record.extentOrRadius = u16le(buf, entry.linear + 2);
        record.partCount = u16le(buf, entry.linear + 10);
        record.partDescriptorPtr = u16le(buf, entry.linear + 12);

        const uint32_t partDescAbs = record.segmentBase + record.partDescriptorPtr;
        requireRange(buf, partDescAbs, static_cast<size_t>(record.partCount) * 8, "part descriptor table");
        const uint8_t firstVertexCount = u8(buf, partDescAbs + 1);
        const uint16_t firstVertexPtr = u16le(buf, partDescAbs + 2);
        const uint32_t vertexAbs = record.segmentBase + firstVertexPtr;
        requireRange(buf, vertexAbs, static_cast<size_t>(firstVertexCount) * 6, "vertex table");
        record.vertices.reserve(firstVertexCount);
        for (uint8_t vi = 0; vi < firstVertexCount; ++vi) {
            const uint32_t v = vertexAbs + static_cast<uint32_t>(vi) * 6u;
            record.vertices.push_back(Vec3i{s16le(buf, v), s16le(buf, v + 2), s16le(buf, v + 4)});
        }

        record.parts.reserve(record.partCount);
        for (uint16_t pi = 0; pi < record.partCount; ++pi) {
            const uint32_t d = partDescAbs + static_cast<uint32_t>(pi) * 8u;
            RuntimePart part;
            part.index = pi;
            part.descriptorOffset = static_cast<uint16_t>(d - record.segmentBase);
            part.startVertex = u8(buf, d);
            part.vertexCount = u8(buf, d + 1);
            part.vertexTablePtr = u16le(buf, d + 2);
            part.variantCount = u16le(buf, d + 4);
            part.variantTablePtr = u16le(buf, d + 6);
            if (part.startVertex < record.vertices.size()) {
                part.sortVertex = record.vertices[part.startVertex];
            }

            const uint32_t vtAbs = record.segmentBase + part.variantTablePtr;
            requireRange(buf, vtAbs, static_cast<size_t>(part.variantCount) * 4, "variant table");
            for (uint16_t vi = 0; vi < part.variantCount; ++vi) {
                const uint32_t vt = vtAbs + static_cast<uint32_t>(vi) * 4u;
                const uint16_t cmdCount = u16le(buf, vt);
                const uint16_t cmdListPtr = u16le(buf, vt + 2);
                const uint32_t cmdAbs = record.segmentBase + cmdListPtr;
                requireRange(buf, cmdAbs, static_cast<size_t>(cmdCount) * 8, "command list");
                for (uint16_t ci = 0; ci < cmdCount; ++ci) {
                    const uint32_t c = cmdAbs + static_cast<uint32_t>(ci) * 8u;
                    RuntimeCommand cmd;
                    cmd.partIndex = pi;
                    cmd.variantIndex = vi;
                    cmd.commandIndex = ci;
                    cmd.offset = static_cast<uint16_t>(c - record.segmentBase);
                    cmd.distanceThreshold = u8(buf, c);
                    cmd.commandType = u8(buf, c + 1);
                    if (cmd.commandType == 0) {
                        cmd.primitiveCount = u16le(buf, c + 2);
                        cmd.primitiveDescriptorPtr = u16le(buf, c + 4);
                        cmd.groupWord = s16le(buf, c + 6);
                        cmd.primitives = parseType0Primitives(buf, record.segmentBase, c, pi, ci);
                    } else if (cmd.commandType == 1) {
                        // OTHPCK impact records 002..007 use the original
                        // radius primitive command: one 16-bit radius, a
                        // resource vertex index and three EGA shade bytes.
                        cmd.radius = u16le(buf, c + 2);
                        cmd.centerVertexIndex = u8(buf, c + 4);
                        cmd.sphereShadeBytes = {
                            u8(buf, c + 5),
                            u8(buf, c + 6),
                            u8(buf, c + 7),
                        };
                    }
                    part.commands.push_back(std::move(cmd));
                }
            }
            record.parts.push_back(std::move(part));
        }

        records.push_back(std::move(record));
    }

    return records;
}

RecordStats recordStats(const RuntimeRecord& record, const std::string& indexMode) {
    RecordStats stats;
    for (const RuntimePart& part : record.parts) {
        for (const RuntimeCommand& cmd : part.commands) {
            for (const RuntimePrimitive& prim : cmd.primitives) {
                std::vector<int> valid;
                valid.reserve(prim.normalizedIndices.size());
                for (uint8_t rawIndex : prim.normalizedIndices) {
                    const int idx = indexMode == "minus1" ? static_cast<int>(rawIndex) - 1 : static_cast<int>(rawIndex);
                    if (idx >= 0 && static_cast<size_t>(idx) < record.vertices.size()) {
                        valid.push_back(idx);
                    }
                }
                if (prim.kind.rfind("polygon", 0) == 0 && valid.size() >= 3) {
                    ++stats.polygonCount;
                    ++stats.primitiveCount;
                } else if (prim.kind == "line" && valid.size() >= 2) {
                    ++stats.lineCount;
                    ++stats.primitiveCount;
                }
            }
        }
    }
    return stats;
}

GpuBatchStats estimateGpuBatchStats(
    const RuntimeRecord& record,
    const std::string& indexMode,
    const std::string& shadeMode,
    bool showLines,
    bool showEdges) {
    GpuBatchStats stats;
    stats.sourceVertexCount = static_cast<int>(record.vertices.size());
    std::set<int> colorKeys;
    constexpr int edgeColorKey = -1;

    for (const RuntimePart& part : record.parts) {
        for (const RuntimeCommand& cmd : part.commands) {
            for (const RuntimePrimitive& prim : cmd.primitives) {
                std::vector<int> valid;
                valid.reserve(prim.normalizedIndices.size());
                for (uint8_t rawIndex : prim.normalizedIndices) {
                    const int idx = indexMode == "minus1" ? static_cast<int>(rawIndex) - 1 : static_cast<int>(rawIndex);
                    if (idx >= 0 && static_cast<size_t>(idx) < record.vertices.size()) {
                        valid.push_back(idx);
                    }
                }
                if (valid.size() < 2) {
                    continue;
                }

                if (prim.kind == "line") {
                    if (showLines && showEdges) {
                        stats.batchVertexCount += static_cast<int>(valid.size());
                        stats.linePairCount += static_cast<int>(valid.size()) - 1;
                        colorKeys.insert(0x00eeeeee);
                    }
                    continue;
                }

                if (prim.kind.rfind("polygon", 0) != 0 || valid.size() < 3) {
                    continue;
                }

                ++stats.polygonCount;
                stats.batchVertexCount += static_cast<int>(valid.size());
                stats.triangleCount += static_cast<int>(valid.size()) - 2;

                if (shadeMode == "ega") {
                    const int shade = ((static_cast<int>(prim.shadeBytes[0]) +
                                        static_cast<int>(prim.shadeBytes[1]) +
                                        static_cast<int>(prim.shadeBytes[2]) +
                                        static_cast<int>(prim.shadeBytes[3]) +
                                        2) /
                                       4) &
                                      0x0f;
                    colorKeys.insert(0x01000000 | shade);
                } else if (shadeMode == "stored") {
                    const double shadeLevel = (static_cast<int>(prim.shadeBytes[0]) +
                                               static_cast<int>(prim.shadeBytes[1]) +
                                               static_cast<int>(prim.shadeBytes[2]) +
                                               static_cast<int>(prim.shadeBytes[3])) /
                                              4.0;
                    int level = 58 + static_cast<int>(std::max(0.0, std::min(15.0, shadeLevel)) * 11.5);
                    if ((prim.materialOrFlags & 0x80u) != 0) {
                        level += 10;
                    }
                    colorKeys.insert(0x02000000 | std::max(0, std::min(255, level)));
                } else {
                    colorKeys.insert(0x03000000 | (prim.partIndex & 0xff));
                }

                if (showEdges) {
                    stats.batchVertexCount += static_cast<int>(valid.size());
                    stats.linePairCount += static_cast<int>(valid.size());
                    colorKeys.insert(edgeColorKey);
                }
            }
        }
    }

    stats.triangleIndexCount = stats.triangleCount * 3;
    stats.lineIndexCount = stats.linePairCount * 2;
    stats.colorCount = static_cast<int>(colorKeys.size());
    return stats;
}

GpuBatch buildGpuBatch(const RuntimeRecord& record, const GpuBatchOptions& options) {
    GpuBatch batch;
    std::vector<Vec3f> transformed;
    transformed.reserve(record.vertices.size());
    for (Vec3i v : record.vertices) {
        transformed.push_back(transformVertex(v, options.scale, options.swizzle));
    }

    auto appendVertex = [&batch](Vec3f p, std::array<float, 3> rgb) {
        batch.vertices.push_back(p.x);
        batch.vertices.push_back(p.y);
        batch.vertices.push_back(p.z);
        batch.vertices.push_back(rgb[0]);
        batch.vertices.push_back(rgb[1]);
        batch.vertices.push_back(rgb[2]);
    };

    const std::array<float, 3> lineRgb{0.94f, 0.94f, 0.94f};

    for (const RuntimePart& part : record.parts) {
        for (const RuntimeCommand& cmd : part.commands) {
            for (const RuntimePrimitive& prim : cmd.primitives) {
                const std::vector<int> valid = validIndices(record, prim, options.indexMode);
                if (valid.size() < 2) {
                    continue;
                }

                if (prim.kind == "line") {
                    if (options.showLines && options.showEdges) {
                        const uint32_t start = static_cast<uint32_t>(batch.vertices.size() / 6u);
                        for (int idx : valid) {
                            appendVertex(transformed[static_cast<size_t>(idx)], lineRgb);
                        }
                        for (size_t i = 0; i + 1 < valid.size(); ++i) {
                            batch.lineIndices.push_back(start + static_cast<uint32_t>(i));
                            batch.lineIndices.push_back(start + static_cast<uint32_t>(i + 1));
                        }
                    }
                    continue;
                }

                if (prim.kind.rfind("polygon", 0) != 0 || valid.size() < 3) {
                    continue;
                }

                const Vec3f p0 = transformed[static_cast<size_t>(valid[0])];
                const Vec3f p1 = transformed[static_cast<size_t>(valid[1])];
                const Vec3f p2 = transformed[static_cast<size_t>(valid[2])];
                const Vec3f normal = normalize(cross(sub(p1, p0), sub(p2, p0)));
                const std::array<float, 3> rgb = primitiveRgb(prim, normal, options.shadeMode);

                const uint32_t start = static_cast<uint32_t>(batch.vertices.size() / 6u);
                for (int idx : valid) {
                    appendVertex(transformed[static_cast<size_t>(idx)], rgb);
                }
                for (size_t j = 1; j + 1 < valid.size(); ++j) {
                    batch.triangleIndices.push_back(start);
                    batch.triangleIndices.push_back(start + static_cast<uint32_t>(j));
                    batch.triangleIndices.push_back(start + static_cast<uint32_t>(j + 1));
                }

                if (options.showEdges) {
                    const uint32_t edgeStart = static_cast<uint32_t>(batch.vertices.size() / 6u);
                    for (int idx : valid) {
                        appendVertex(transformed[static_cast<size_t>(idx)], options.edgeRgb);
                    }
                    for (size_t j = 0; j < valid.size(); ++j) {
                        batch.lineIndices.push_back(edgeStart + static_cast<uint32_t>(j));
                        batch.lineIndices.push_back(edgeStart + static_cast<uint32_t>((j + 1) % valid.size()));
                    }
                }
            }
        }
    }

    std::set<int> groups;
    for (const RuntimePart& part : record.parts) {
        for (const RuntimeCommand& cmd : part.commands) {
            for (const RuntimePrimitive& prim : cmd.primitives) {
                groups.insert(prim.partIndex);
            }
        }
    }
    for (uint16_t group = 0; group < record.partCount; ++group) {
        groups.insert(static_cast<int>(group));
    }

    for (int group : groups) {
        std::array<float, 16> matrix{};
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
        batch.groupMatrices.emplace(group, matrix);
    }

    return batch;
}

} // namespace mw::legacy3d
