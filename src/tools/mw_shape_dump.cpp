#include "legacy3d/shape_parser.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cerr
        << "usage: mw_shape_dump [--record N] [--index-mode raw|minus1] [--shade-mode stored|ega|lit] "
           "[--edge-rgb R,G,B] [--dump-gpu-dir DIR] [--dump-ir-dir DIR] <resource.tbl>...\n";
}

std::optional<int> parseInt(const std::string& text) {
    try {
        size_t used = 0;
        const int value = std::stoi(text, &used, 10);
        if (used != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::array<float, 3>> parseRgb(const std::string& text) {
    std::array<float, 3> rgb{};
    std::stringstream ss(text);
    std::string item;
    for (size_t i = 0; i < rgb.size(); ++i) {
        if (!std::getline(ss, item, ',')) {
            return std::nullopt;
        }
        try {
            size_t used = 0;
            rgb[i] = std::stof(item, &used);
            if (used != item.size()) {
                return std::nullopt;
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    if (std::getline(ss, item, ',')) {
        return std::nullopt;
    }
    return rgb;
}

void dumpRecord(const mw::legacy3d::RuntimeRecord& record, const std::string& indexMode, const std::string& shadeMode) {
    const mw::legacy3d::RecordStats raw = mw::legacy3d::recordStats(record, indexMode);
    const mw::legacy3d::GpuBatchStats gpu = mw::legacy3d::estimateGpuBatchStats(record, indexMode, shadeMode);

    std::cout
        << "record"
        << "\tresource=" << record.resourceName
        << "\tindex=" << record.recordIndex
        << "\toffset=0x" << std::hex << record.recordOffset
        << "\tsegment=0x" << record.recordSegment
        << std::dec
        << "\tsource_vertices=" << record.vertices.size()
        << "\tparts=" << record.parts.size()
        << "\traw_poly=" << raw.polygonCount
        << "\traw_line=" << raw.lineCount
        << "\tgpu_vertices=" << gpu.batchVertexCount
        << "\tgpu_triangles=" << gpu.triangleCount
        << "\tgpu_triangle_indices=" << gpu.triangleIndexCount
        << "\tgpu_line_pairs=" << gpu.linePairCount
        << "\tgpu_line_indices=" << gpu.lineIndexCount
        << "\tgpu_colors=" << gpu.colorCount
        << "\n";
}

template <typename T>
void writeVectorBinary(const std::filesystem::path& path, const std::vector<T>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not open output file: " + path.string());
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(T)));
    }
}

void dumpGpuBuffers(
    const mw::legacy3d::RuntimeRecord& record,
    const std::filesystem::path& outDir,
    const mw::legacy3d::GpuBatchOptions& options) {
    std::filesystem::create_directories(outDir);
    const mw::legacy3d::GpuBatch batch = mw::legacy3d::buildGpuBatch(record, options);
    const std::string prefix = record.resourceName + "_record_" +
                               (record.recordIndex < 10 ? "00" : record.recordIndex < 100 ? "0" : "") +
                               std::to_string(record.recordIndex);

    writeVectorBinary(outDir / (prefix + "_vertices_float32.bin"), batch.vertices);
    writeVectorBinary(outDir / (prefix + "_triangle_indices_uint32.bin"), batch.triangleIndices);
    writeVectorBinary(outDir / (prefix + "_line_indices_uint32.bin"), batch.lineIndices);

    std::vector<float> matrices;
    matrices.reserve(batch.groupMatrices.size() * 16u);
    for (const auto& groupAndMatrix : batch.groupMatrices) {
        matrices.insert(matrices.end(), groupAndMatrix.second.begin(), groupAndMatrix.second.end());
    }
    writeVectorBinary(outDir / (prefix + "_group_matrices_float32.bin"), matrices);

    std::cout
        << "gpu_dump"
        << "\tresource=" << record.resourceName
        << "\tindex=" << record.recordIndex
        << "\tdir=" << outDir.string()
        << "\tvertices_file=" << prefix << "_vertices_float32.bin"
        << "\ttriangles_file=" << prefix << "_triangle_indices_uint32.bin"
        << "\tlines_file=" << prefix << "_line_indices_uint32.bin"
        << "\tmatrices_file=" << prefix << "_group_matrices_float32.bin"
        << "\n";
}

std::string recordFilePrefix(const mw::legacy3d::RuntimeRecord& record) {
    return record.resourceName + "_record_" +
           (record.recordIndex < 10 ? "00" : record.recordIndex < 100 ? "0" : "") +
           std::to_string(record.recordIndex);
}

std::string hex16(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setw(4) << std::setfill('0') << (value & 0xffffu);
    return oss.str();
}

template <typename T>
void writeNumberArray(std::ostream& out, const std::vector<T>& values) {
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << static_cast<int>(values[i]);
    }
    out << "]";
}

void dumpRuntimeIr(const mw::legacy3d::RuntimeRecord& record, const std::filesystem::path& outDir) {
    std::filesystem::create_directories(outDir);
    const std::string prefix = recordFilePrefix(record);
    const std::filesystem::path path = outDir / (prefix + "_runtime_ir.json");
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not open IR output file: " + path.string());
    }

    out << "{\n";
    out << "  \"resource\": \"" << record.resourceName << "\",\n";
    out << "  \"record_index\": " << record.recordIndex << ",\n";
    out << "  \"record_offset\": " << record.recordOffset << ",\n";
    out << "  \"record_offset_hex\": \"" << hex16(record.recordOffset) << "\",\n";
    out << "  \"record_segment\": " << record.recordSegment << ",\n";
    out << "  \"record_segment_hex\": \"" << hex16(record.recordSegment) << "\",\n";
    out << "  \"segment_base\": " << record.segmentBase << ",\n";
    out << "  \"flags\": " << static_cast<int>(record.flags) << ",\n";
    out << "  \"scale_shift\": " << static_cast<int>(record.scaleShift) << ",\n";
    out << "  \"extent_or_radius\": " << record.extentOrRadius << ",\n";
    out << "  \"part_count\": " << record.partCount << ",\n";
    out << "  \"part_descriptor_ptr\": " << record.partDescriptorPtr << ",\n";
    out << "  \"part_descriptor_ptr_hex\": \"" << hex16(record.partDescriptorPtr) << "\",\n";
    out << "  \"vertices\": [\n";
    for (size_t i = 0; i < record.vertices.size(); ++i) {
        const auto& v = record.vertices[i];
        out << "    {\"index\": " << i << ", \"x\": " << v.x << ", \"y\": " << v.y << ", \"z\": " << v.z << "}";
        out << (i + 1 == record.vertices.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"parts\": [\n";
    for (size_t pi = 0; pi < record.parts.size(); ++pi) {
        const auto& part = record.parts[pi];
        out << "    {\n";
        out << "      \"index\": " << part.index << ",\n";
        out << "      \"descriptor_offset\": " << part.descriptorOffset << ",\n";
        out << "      \"descriptor_offset_hex\": \"" << hex16(part.descriptorOffset) << "\",\n";
        out << "      \"start_vertex\": " << static_cast<int>(part.startVertex) << ",\n";
        out << "      \"vertex_count\": " << static_cast<int>(part.vertexCount) << ",\n";
        out << "      \"vertex_table_ptr\": " << part.vertexTablePtr << ",\n";
        out << "      \"vertex_table_ptr_hex\": \"" << hex16(part.vertexTablePtr) << "\",\n";
        out << "      \"variant_count\": " << part.variantCount << ",\n";
        out << "      \"variant_table_ptr\": " << part.variantTablePtr << ",\n";
        out << "      \"variant_table_ptr_hex\": \"" << hex16(part.variantTablePtr) << "\",\n";
        out << "      \"sort_vertex\": {\"x\": " << part.sortVertex.x << ", \"y\": " << part.sortVertex.y << ", \"z\": " << part.sortVertex.z << "},\n";
        out << "      \"commands\": [\n";
        for (size_t ci = 0; ci < part.commands.size(); ++ci) {
            const auto& cmd = part.commands[ci];
            out << "        {\n";
            out << "          \"part_index\": " << cmd.partIndex << ",\n";
            out << "          \"variant_index\": " << cmd.variantIndex << ",\n";
            out << "          \"command_index\": " << cmd.commandIndex << ",\n";
            out << "          \"offset\": " << cmd.offset << ",\n";
            out << "          \"offset_hex\": \"" << hex16(cmd.offset) << "\",\n";
            out << "          \"distance_threshold\": " << static_cast<int>(cmd.distanceThreshold) << ",\n";
            out << "          \"command_type\": " << static_cast<int>(cmd.commandType) << ",\n";
            out << "          \"primitive_count\": " << cmd.primitiveCount << ",\n";
            out << "          \"primitive_descriptor_ptr\": " << cmd.primitiveDescriptorPtr << ",\n";
            out << "          \"primitive_descriptor_ptr_hex\": \"" << hex16(cmd.primitiveDescriptorPtr) << "\",\n";
            out << "          \"group_word\": " << cmd.groupWord << ",\n";
            out << "          \"primitives\": [\n";
            for (size_t pri = 0; pri < cmd.primitives.size(); ++pri) {
                const auto& prim = cmd.primitives[pri];
                out << "            {";
                out << "\"part_index\": " << prim.partIndex;
                out << ", \"command_index\": " << prim.commandIndex;
                out << ", \"primitive_index\": " << prim.primitiveIndex;
                out << ", \"descriptor_offset\": " << prim.descriptorOffset;
                out << ", \"descriptor_offset_hex\": \"" << hex16(prim.descriptorOffset) << "\"";
                out << ", \"opcode\": " << static_cast<int>(prim.opcode);
                out << ", \"shade_bytes\": [";
                for (size_t si = 0; si < prim.shadeBytes.size(); ++si) {
                    if (si != 0) {
                        out << ",";
                    }
                    out << static_cast<int>(prim.shadeBytes[si]);
                }
                out << "]";
                out << ", \"material_or_flags\": " << static_cast<int>(prim.materialOrFlags);
                out << ", \"index_list_offset\": " << prim.indexListOffset;
                out << ", \"index_list_offset_hex\": \"" << hex16(prim.indexListOffset) << "\"";
                out << ", \"raw_indices\": ";
                writeNumberArray(out, prim.rawIndices);
                out << ", \"normalized_indices\": ";
                writeNumberArray(out, prim.normalizedIndices);
                out << ", \"kind\": \"" << prim.kind << "\"";
                out << "}";
                out << (pri + 1 == cmd.primitives.size() ? "\n" : ",\n");
            }
            out << "          ]\n";
            out << "        }";
            out << (ci + 1 == part.commands.size() ? "\n" : ",\n");
        }
        out << "      ]\n";
        out << "    }";
        out << (pi + 1 == record.parts.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";

    std::cout
        << "ir_dump"
        << "\tresource=" << record.resourceName
        << "\tindex=" << record.recordIndex
        << "\tdir=" << outDir.string()
        << "\tfile=" << prefix << "_runtime_ir.json"
        << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::optional<int> selectedRecord;
    std::string indexMode = "raw";
    std::string shadeMode = "stored";
    std::array<float, 3> edgeRgb{0.02f, 0.02f, 0.02f};
    std::optional<std::filesystem::path> dumpGpuDir;
    std::optional<std::filesystem::path> dumpIrDir;

    int argi = 1;
    for (; argi < argc; ++argi) {
        const std::string arg = argv[argi];
        if (arg == "--record") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            selectedRecord = parseInt(argv[++argi]);
            if (!selectedRecord) {
                printUsage();
                return 2;
            }
        } else if (arg == "--index-mode") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            indexMode = argv[++argi];
            if (indexMode != "raw" && indexMode != "minus1") {
                printUsage();
                return 2;
            }
        } else if (arg == "--shade-mode") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            shadeMode = argv[++argi];
            if (shadeMode != "stored" && shadeMode != "ega" && shadeMode != "lit") {
                printUsage();
                return 2;
            }
        } else if (arg == "--edge-rgb") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            const std::optional<std::array<float, 3>> parsed = parseRgb(argv[++argi]);
            if (!parsed) {
                printUsage();
                return 2;
            }
            edgeRgb = *parsed;
        } else if (arg == "--dump-gpu-dir") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            dumpGpuDir = std::filesystem::path(argv[++argi]);
        } else if (arg == "--dump-ir-dir") {
            if (argi + 1 >= argc) {
                printUsage();
                return 2;
            }
            dumpIrDir = std::filesystem::path(argv[++argi]);
        } else if (!arg.empty() && arg[0] == '-') {
            printUsage();
            return 2;
        } else {
            break;
        }
    }

    if (argi >= argc) {
        printUsage();
        return 2;
    }

    try {
        for (; argi < argc; ++argi) {
            const std::filesystem::path path(argv[argi]);
            const auto records = mw::legacy3d::loadRuntimeShapeRecords(path);
            std::cout << "resource\tpath=" << path.string() << "\trecords=" << records.size() << "\n";
            for (const mw::legacy3d::RuntimeRecord& record : records) {
                if (selectedRecord && record.recordIndex != *selectedRecord) {
                    continue;
                }
                dumpRecord(record, indexMode, shadeMode);
                if (dumpGpuDir) {
                    mw::legacy3d::GpuBatchOptions options;
                    options.indexMode = indexMode;
                    options.shadeMode = shadeMode;
                    options.edgeRgb = edgeRgb;
                    dumpGpuBuffers(record, *dumpGpuDir, options);
                }
                if (dumpIrDir) {
                    dumpRuntimeIr(record, *dumpIrDir);
                }
            }
        }
    } catch (const std::exception& exc) {
        std::cerr << "mw_shape_dump: " << exc.what() << "\n";
        return 1;
    }

    return 0;
}
