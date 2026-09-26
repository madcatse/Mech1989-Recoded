#include "legacy3d/terrain_parser.h"
#include "legacy3d/shape_parser.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error(
                "usage: mw_terpck_gi_collision_smoke TERPCK.GI TERPCK.TBL TILE0.WLD");
        }

        const mw::legacy3d::TerpckGiResource resource =
            mw::legacy3d::loadTerpckGiResource(std::filesystem::path(argv[1]));
        const std::vector<mw::legacy3d::RuntimeRecord> renderRecords =
            mw::legacy3d::loadRuntimeShapeRecords(std::filesystem::path(argv[2]));
        const mw::legacy3d::TerrainWorld world =
            mw::legacy3d::loadTerrainWorld(std::filesystem::path(argv[3]));
        size_t activeRecords = 0;
        size_t subrecords = 0;
        size_t edges = 0;
        size_t maximumEdges = 0;
        for (const mw::legacy3d::TerpckGiCollisionRecord& record : resource.collisionRecords) {
            activeRecords += record.subrecords.empty() ? 0u : 1u;
            subrecords += record.subrecords.size();
            for (const mw::legacy3d::TerpckGiCollisionSubrecord& subrecord : record.subrecords) {
                edges += subrecord.edges.size();
                maximumEdges = std::max(maximumEdges, subrecord.edges.size());
            }
        }
        require(resource.collisionRecords.size() == 30u, "unexpected collision record count");
        require(activeRecords == 29u, "unexpected active collision record count");
        require(subrecords == 376u, "unexpected collision subrecord count");
        require(edges == 1227u, "unexpected collision edge count");
        require(maximumEdges == 6u, "unexpected maximum collision edge count");
        constexpr std::array<size_t, 4> kPriority0Result0{{26u, 27u, 28u, 29u}};
        constexpr std::array<size_t, 16> kPriority0Result2{{
            2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
            10u, 11u, 18u, 19u, 20u, 21u, 22u, 23u,
        }};
        constexpr std::array<size_t, 10> kPriority5Result1{{
            0u, 1u, 12u, 13u, 14u, 15u, 16u, 17u, 24u, 25u,
        }};
        for (size_t index : kPriority0Result0) {
            const auto& record = resource.collisionRecords[index];
            require(record.priorityByte5 == 0u && record.resultByte6 == 0u,
                    "priority-zero/result-zero collision row changed");
        }
        for (size_t index : kPriority0Result2) {
            const auto& record = resource.collisionRecords[index];
            require(record.priorityByte5 == 0u && record.resultByte6 == 2u,
                    "priority-zero/result-two collision row changed");
        }
        for (size_t index : kPriority5Result1) {
            const auto& record = resource.collisionRecords[index];
            require(record.priorityByte5 == 5u && record.resultByte6 == 1u,
                    "priority-five/result-one collision row changed");
        }
        require(renderRecords.size() == resource.collisionRecords.size(), "TERPCK record counts differ");
        size_t shiftedRenderRecords = 0;
        for (const mw::legacy3d::RuntimeRecord& record : renderRecords) {
            shiftedRenderRecords += record.scaleShift == 0u ? 0u : 1u;
        }
        require(shiftedRenderRecords == 1u, "unexpected shifted TERPCK render record count");
        require(renderRecords[1].scaleShift == 3u, "TERPCK record 1 scale shift is not three");
        require(world.objects.size() == 3u, "unexpected TILE0 WLD record count");
        size_t worldCenterMatches = 0;
        for (const mw::legacy3d::TerrainWorldObject& object : world.objects) {
            require(object.recordIndex < resource.collisionRecords.size(), "WLD record index is out of range");
            require(object.y == 0, "shipped TILE0 WLD third coordinate is not zero");
            const mw::legacy3d::TerpckGiPlanarQueryResult center =
                mw::legacy3d::terpckGiQueryRecordPlanar(
                    resource.collisionRecords[object.recordIndex],
                    object.x,
                    object.z,
                    object.x,
                    object.z,
                    renderRecords[object.recordIndex].scaleShift);
            require(center.broadPhaseAccepted, "WLD object center failed its broad phase");
            worldCenterMatches += center.subrecordIndex.has_value() ? 1u : 0u;
        }

        mw::legacy3d::TerpckGiCollisionSubrecord square;
        square.edges = {
            {1, 0, 100, 0},
            {-1, 0, -100, 0},
            {0, 1, 0, 100},
            {0, -1, 0, -100},
        };
        require(
            mw::legacy3d::terpckGiSubrecordContainsPoint(square, 0, 0),
            "square interior was rejected");
        require(
            mw::legacy3d::terpckGiSubrecordContainsPoint(square, 100, 100),
            "strict zero boundary was rejected");
        require(
            !mw::legacy3d::terpckGiSubrecordContainsPoint(square, 101, 0),
            "strict positive edge expression was accepted");

        mw::legacy3d::TerpckGiCollisionSubrecord wrappedDifference;
        wrappedDifference.edges = {{1, 0, -32768, 0}};
        require(
            mw::legacy3d::terpckGiSubrecordContainsPoint(wrappedDifference, 32767, 0),
            "signed 16-bit coordinate subtraction did not wrap");

        mw::legacy3d::TerpckGiCollisionSubrecord noEdges;
        require(
            !mw::legacy3d::terpckGiSubrecordContainsPoint(noEdges, 0, 0),
            "zero-edge subrecord was accepted");
        mw::legacy3d::TerpckGiCollisionSubrecord tooManyEdges;
        tooManyEdges.edges.resize(33u);
        require(
            !mw::legacy3d::terpckGiSubrecordContainsPoint(tooManyEdges, 0, 0),
            "subrecord above the original 32-edge limit was accepted");

        mw::legacy3d::TerpckGiCollisionRecord planarRecord;
        planarRecord.extentX = 100u;
        planarRecord.extentZ = 100u;
        planarRecord.subrecords.push_back(square);
        const mw::legacy3d::TerpckGiPlanarQueryResult extentBoundary =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, 100, 100, 0, 0, 0);
        require(extentBoundary.broadPhaseAccepted, "inclusive broad-phase boundary was rejected");
        require(extentBoundary.subrecordIndex == 0u, "boundary subrecord was not selected");
        const mw::legacy3d::TerpckGiPlanarQueryResult extentOutside =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, 101, 0, 0, 0, 0);
        require(!extentOutside.broadPhaseAccepted, "outside broad-phase point was accepted");
        const mw::legacy3d::TerpckGiPlanarQueryResult shiftedBoundary =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, 201, 0, 0, 0, 1);
        require(
            shiftedBoundary.scaledDeltaX == 100 && shiftedBoundary.subrecordIndex == 0u,
            "arithmetic scale shift did not reach the boundary");
        const mw::legacy3d::TerpckGiPlanarQueryResult shiftedOutside =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, 202, 0, 0, 0, 1);
        require(!shiftedOutside.broadPhaseAccepted, "scaled outside point was accepted");
        const mw::legacy3d::TerpckGiPlanarQueryResult negativeShiftedBoundary =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, -199, 0, 0, 0, 1);
        require(
            negativeShiftedBoundary.scaledDeltaX == -100 &&
                negativeShiftedBoundary.subrecordIndex == 0u,
            "negative arithmetic scale shift did not round down to the boundary");
        const mw::legacy3d::TerpckGiPlanarQueryResult negativeShiftedOutside =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, -201, 0, 0, 0, 1);
        require(
            negativeShiftedOutside.scaledDeltaX == -101 &&
                !negativeShiftedOutside.broadPhaseAccepted,
            "negative arithmetic scale shift outside boundary was accepted");

        mw::legacy3d::TerpckGiCollisionSubrecord innerSquare;
        innerSquare.edges = {
            {1, 0, 10, 0},
            {-1, 0, -10, 0},
            {0, 1, 0, 10},
            {0, -1, 0, -10},
        };
        planarRecord.subrecords.push_back(innerSquare);
        const mw::legacy3d::TerpckGiPlanarQueryResult cachedHit =
            mw::legacy3d::terpckGiQueryRecordPlanar(
                planarRecord, 0, 0, 0, 0, 0, static_cast<uint8_t>(1));
        require(
            cachedHit.cachedSubrecordTested && cachedHit.cachedSubrecordAccepted &&
                cachedHit.testedSubrecords == 1u && cachedHit.subrecordIndex == 1u,
            "cached subrecord hit was not retained");
        const mw::legacy3d::TerpckGiPlanarQueryResult cachedMiss =
            mw::legacy3d::terpckGiQueryRecordPlanar(
                planarRecord, 50, 0, 0, 0, 0, static_cast<uint8_t>(1));
        require(
            cachedMiss.cachedSubrecordTested && !cachedMiss.cachedSubrecordAccepted &&
                cachedMiss.testedSubrecords == 2u && cachedMiss.subrecordIndex == 0u,
            "cached miss did not restart ordered subrecord search");

        planarRecord.modeByte4 = 1u;
        const mw::legacy3d::TerpckGiPlanarQueryResult numericMode =
            mw::legacy3d::terpckGiQueryRecordPlanar(planarRecord, 50, 0, 0, 0, 0);
        require(
            numericMode.modeBypassedNarrowPhase && numericMode.subrecordIndex == 0u &&
                numericMode.testedSubrecords == 0u,
            "nonzero numeric mode did not bypass narrow phase");

        mw::legacy3d::TerpckGiCollisionSubrecord heightProbe;
        heightProbe.plane = {1000, 0, 1000, 0};
        heightProbe.heightScaleByte6 = 64u;
        heightProbe.baseHeightWord = 10;
        require(
            mw::legacy3d::terpckGiSubrecordHeightWord(heightProbe, 0, 0) == 986,
            "positive D330 plane height is incorrect");
        require(
            mw::legacy3d::terpckGiSubrecordHeightWord(heightProbe, 1000, 0) == 10,
            "zero D330 expression did not preserve base height");
        const int16_t negativeHeight =
            mw::legacy3d::terpckGiSubrecordHeightWord(heightProbe, 2000, 0);
        require(negativeHeight == -967, "negative D330 high product word is incorrect");
        require(
            mw::legacy3d::terpckGiScaleHeightToWorld(negativeHeight, 1u, 100) == -1834,
            "f21a scale shift and WLD third-coordinate addition are incorrect");

        mw::legacy3d::TerpckGiCollisionSubrecord correctionProbe;
        correctionProbe.byte7 = 64u;
        correctionProbe.byte8 = 0u;
        const mw::legacy3d::TerpckGiContactCorrection stableCorrection =
            mw::legacy3d::terpckGiContactCorrection(correctionProbe, 64u, 0u, 0u);
        require(
            stableCorrection.desiredByte0 == 64u && stableCorrection.desiredByte1 == 0u &&
                stableCorrection.controlWord30 == 0 && stableCorrection.controlWord32 == 0,
            "stable c42e desired bytes produced a control step");
        const mw::legacy3d::TerpckGiContactCorrection boundedCorrection =
            mw::legacy3d::terpckGiContactCorrection(correctionProbe, 0u, 1u, 0u);
        require(
            boundedCorrection.controlWord30 == 0x0100 &&
                boundedCorrection.controlWord32 == -0x0100,
            "c42e signed byte corrections are not bounded to one step");
        const mw::legacy3d::TerpckGiContactCorrection wrappedCorrection =
            mw::legacy3d::terpckGiContactCorrection(correctionProbe, 0u, 0u, 128u);
        require(
            wrappedCorrection.desiredByte0 == 192u && wrappedCorrection.desiredByte1 == 0u &&
                wrappedCorrection.controlWord30 == -0x0100,
            "c42e wrapped-byte fold is incorrect");

        std::cout
            << "phase13_terpck_gi_collision=BTECH210f+ad32+d3e6+d184+d330"
            << ":records30:subrecords376:edges1227:strict_boundary:wrap16"
            << ":wld3:center_hits" << worldCenterMatches
            << ":broad_inclusive:scale_shift:cache_order:height_signed:contact_steps"
            << ":result_rows0_2_1\n";
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "mw_terpck_gi_collision_smoke: " << exc.what() << "\n";
        return 1;
    }
}
