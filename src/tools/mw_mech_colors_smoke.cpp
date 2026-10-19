#include "battle/battle_module.h"
#include "battle/battle_setup.h"
#include "mech3d/mech_catalog.h"
#include "legacy3d/terrain_parser.h"
#include "presentation/campaign_battle_gl_viewport.h"
#include "presentation/campaign_cockpit_compositor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mw;
size_t checks = 0;
void require(bool value, const std::string& reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
legacy3d::OriginalEgaColorOptions palette(const std::string& name) {
    legacy3d::OriginalEgaColorOptions result;
    for (size_t bank = 0; bank < 2; ++bank) {
        const auto p = presentation::loadCampaignEgaPaletteBank(
            std::filesystem::path("Sorted Original Files/PAL") / (name+".PAL"), bank);
        require(p.valid, p.reason);
        std::copy(p.colorPairs.begin(), p.colorPairs.end(), result.colorPairs.begin()+bank*16);
    }
    return result;
}
std::array<float, 3> rgb(const legacy3d::GpuBatch& batch, uint32_t index) {
    const size_t i = static_cast<size_t>(index)*6u;
    return {batch.vertices.at(i+3), batch.vertices.at(i+4), batch.vertices.at(i+5)};
}
void sameGeometry(const legacy3d::GpuBatch& a, const legacy3d::GpuBatch& b) {
    bool same = a.vertices.size() == b.vertices.size();
    if (same) for (size_t i = 0; i < a.vertices.size(); i += 6u)
        for (size_t j = 0; j < 3; ++j) same = same && a.vertices[i+j] == b.vertices[i+j];
    require(same && a.triangleIndices == b.triangleIndices && a.lineIndices == b.lineIndices &&
            a.groupMatrices == b.groupMatrices, "color mapping changed geometry/contract");
}
void descriptorChecks() {
    legacy3d::RuntimeRecord record;
    record.vertices = {{0,0,0},{50,0,0},{0,50,0}};
    legacy3d::RuntimePrimitive polygon;
    polygon.kind = "polygon"; polygon.opcode = 0x81;
    polygon.normalizedIndices = {0,1,2}; polygon.shadeBytes = {12,14,1,4};
    auto line = polygon; line.kind = "line"; line.normalizedIndices = {0,1};
    legacy3d::RuntimeCommand command; command.primitives = {polygon,line};
    legacy3d::RuntimePart part; part.commands = {command}; record.parts = {part};
    legacy3d::GpuBatchOptions options; options.originalEga = palette("TROPIC");
    const auto old = legacy3d::buildGpuBatch(record);
    const auto colored = legacy3d::buildGpuBatch(record,options);
    sameGeometry(old,colored);
    require(rgb(colored,colored.triangleIndices.front()) == std::array<float,3>{170.f/255.f,0,0}, "EGA fill must use descriptor +4");
    for (auto i : colored.lineIndices)
        require(rgb(colored,i) == std::array<float,3>{0,0,170.f/255.f}, "EGA outline/line must use descriptor +3");
    record.parts[0].commands[0].primitives[0].shadeBytes = {255,255,0,23};
    const auto low = legacy3d::buildGpuBatch(record,options);
    options.originalEga->alternatePixels = true;
    const auto high = legacy3d::buildGpuBatch(record,options);
    sameGeometry(low,high);
    require(options.originalEga->colorPairs[23] == 0x72, "TROPIC logical 23 palette fixture");
    require(rgb(low,low.triangleIndices.front()) == std::array<float,3>{0,170.f/255.f,0} &&
            rgb(high,high.triangleIndices.front()) == std::array<float,3>{170.f/255.f,170.f/255.f,170.f/255.f}, "pixel pair must retain both colors, without averaging");
    require(rgb(low,low.lineIndices.front()) == std::array<float,3>{0,0,0} &&
            rgb(high,high.lineIndices.front()) == std::array<float,3>{0,0,0}, "black outline became gray or patterned");
    record.parts[0].commands[0].primitives[0].shadeBytes[3] = 32;
    bool rejected = false;
    try { legacy3d::buildGpuBatch(record,options); } catch (const legacy3d::ParseError&) { rejected = true; }
    require(rejected,"out-of-range palette index must fail closed");
    options.originalEga.reset();
    require(!legacy3d::buildGpuBatch(record,options).vertices.empty(), "legacy diagnostic path changed");
}
void catalogChecks() {
    const auto entries = mech3d::mechCatalogEntries();
    require(entries.size() == 8, "eight original chassis required");
    for (const auto& entry : entries) {
        const auto records = legacy3d::loadRuntimeShapeRecords(entry.defaultResourcePath);
        for (const auto& biome : {"DESERT","TROPIC","ARCTIC"}) {
            legacy3d::GpuBatchOptions options; options.originalEga = palette(biome);
            bool blue = false, red = false, yellow = false;
            for (const auto& record : records) {
                const auto old = legacy3d::buildGpuBatch(record);
                const auto colored = legacy3d::buildGpuBatch(record,options);
                sameGeometry(old,colored);
                options.originalEga->alternatePixels = true;
                const auto alternate = legacy3d::buildGpuBatch(record,options);
                sameGeometry(colored,alternate);
                options.originalEga->alternatePixels = false;
                for (auto i : colored.triangleIndices) {
                    const auto c = rgb(colored,i);
                    blue = blue || c == std::array<float,3>{0,0,170.f/255.f};
                    red = red || c == std::array<float,3>{170.f/255.f,0,0};
                    yellow = yellow || c == std::array<float,3>{1,1,85.f/255.f};
                }
            }
            if (entry.presetId == "jenner" || entry.presetId == "rifleman") require(blue,entry.presetId+" lost source blue");
            if (entry.presetId == "warhammer" || entry.presetId == "marauder") require(red,entry.presetId+" lost source red");
            if (entry.presetId == "locust") require(yellow,"Locust lost source yellow");
        }
    }
}
void savePpm(const std::string& name, const std::vector<uint32_t>& pixels,
             const std::filesystem::path& directory = "outputs/mech_color_audit") {
    std::filesystem::create_directories(directory);
    std::ofstream out(directory/(name+".ppm"), std::ios::binary);
    out << "P6\n640 480\n255\n";
    for (auto c : pixels) { const char p[] = {char(c>>16),char(c>>8),char(c)}; out.write(p,3); }
    require(bool(out),"could not save WGL readback");
}
void sceneChecks(bool visual) {
    using namespace battle;
    using namespace presentation;
    BattleStartParams params;
    params.terrainScenarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
    params.terrainScenarioIndex = 2; params.terrainEnvironmentId = 1;
    const auto setup = decodeOriginalBattlefieldSetup(params.terrainScenarioPath,2,8,params.terrainCellSize);
    params.playerStartTransform = setup.playerStartTransform;
    BattleCombatantLaunchState enemy;
    enemy.roster.team = BattleTeam::Opposing; enemy.roster.sourceSlot = "opposing:0";
    enemy.startTransform = {95000,0,50000,0};
    params.combatantLaunchStates.push_back(enemy);
    const auto world = BattleWorld::create(params);
    const auto snapshot = world.snapshot();
    const auto fingerprint = battleSnapshotFingerprint(snapshot);
    const auto baseScene = campaignBattleRenderSceneFromSnapshot(snapshot,CampaignBattlePresentationMode::External,"Sorted Original Files");
    require(baseScene.valid && !baseScene.combatantVisuals.empty(),baseScene.reason);
    // Place the diagnostic actor on the rendered terrain, not at arbitrary Y=0.
    std::array<legacy3d::TerrainGrid,4> grids;
    for (size_t i = 0; i < grids.size(); ++i) grids[i] = legacy3d::loadTerrainGrid(baseScene.terrain.gridPaths.at(i));
    const auto grid = legacy3d::composeTerrainGrid2x2(grids,"color_probe",false,true);
    const float cell = static_cast<float>((baseScene.terrain.boundsMaxX-baseScene.terrain.boundsMinX)/(grid.width-1));
    const auto ground = legacy3d::buildTerrainMesh(grid,cell,20.f);
    CampaignBattleGlViewport viewport;
    HWND parent = nullptr;
    bool cameraConfigured = false;
    const auto configureCamera = [](HWND child) {
        require(child != nullptr,"WGL child missing");
        SendMessageW(child,WM_RBUTTONDOWN,0,MAKELPARAM(0,0));
        SendMessageW(child,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(-536,-67));
        SendMessageW(child,WM_RBUTTONUP,0,MAKELPARAM(-536,-67));
    };
    if (visual) {
        parent = CreateWindowExW(0,L"STATIC",L"Mech color render probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"WGL initialize failed");
        viewport.setBounds(RECT{0,0,640,480});
    }
    for (const auto& entry : mech3d::mechCatalogEntries()) {
        auto scene = baseScene;
        scene.combatantVisuals.resize(1);
        auto& actor = scene.combatantVisuals.front();
        actor.mechPresetId = entry.presetId; actor.modelResourcePath = entry.defaultResourcePath;
        actor.transform.y = ground.sampleHeightNearest(
            static_cast<float>(actor.transform.x-scene.terrain.boundsMinX),
            static_cast<float>(actor.transform.z-scene.terrain.boundsMinZ));
        const auto probe = probeCampaignBattleGlResources(scene);
        require(probe.valid && probe.gpuBatchContractPreserved,entry.presetId+": "+probe.reason);
        require(probe.mechColoredVertexCount > 0,entry.presetId+" still monochrome");
        if (entry.presetId == "locust") require(probe.mechDitherVertexCount > 0,"Locust palette pair missing");
        actor.team = BattleTeam::Opposing;
        const auto opponent = probeCampaignBattleGlResources(scene);
        require(opponent.valid && opponent.mechColoredVertexCount == probe.mechColoredVertexCount &&
                opponent.mechDitherVertexCount == probe.mechDitherVertexCount,"team changed model colors");
        actor.team = BattleTeam::Player;
        if (!visual) continue;
        require(viewport.updateScene(scene),viewport.lastError());
        if (!cameraConfigured) {
            configureCamera(FindWindowExW(parent,nullptr,L"MWCampaignBattleGlViewport",nullptr));
            cameraConfigured = true;
        }
        const auto pixels = viewport.captureScenePixels();
        require(pixels.size() == 640*480 && pixels == viewport.captureScenePixels(),"scene readback is empty or cadence-dependent");
        savePpm(entry.presetId,pixels);
        actor.team = BattleTeam::Opposing;
        require(viewport.updateScene(scene),viewport.lastError());
        require(pixels == viewport.captureScenePixels(),"team recolored WGL model");
        actor.team = BattleTeam::Player;
        // Reuse the same entity/frame cache across palette changes, then restore it.
        scene.terrain.palettePath = "Sorted Original Files/PAL/ARCTIC.PAL";
        scene.terrain.environmentId = 2;
        require(viewport.updateScene(scene),viewport.lastError());
        const auto changed = viewport.captureScenePixels();
        CampaignBattleGlViewport fresh;
        const HWND freshParent = CreateWindowExW(0,L"STATIC",L"Palette cache probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(freshParent && fresh.initialize(GetModuleHandleW(nullptr),freshParent),"fresh WGL initialize failed");
        fresh.setBounds(RECT{0,0,640,480});
        require(fresh.updateScene(scene),fresh.lastError());
        configureCamera(FindWindowExW(freshParent,nullptr,L"MWCampaignBattleGlViewport",nullptr));
        require(changed == fresh.captureScenePixels(),"stale mech color palette in reused cache");
        fresh.shutdown(); DestroyWindow(freshParent);
        scene.terrain.palettePath = baseScene.terrain.palettePath;
        scene.terrain.environmentId = baseScene.terrain.environmentId;
        require(viewport.updateScene(scene),viewport.lastError());
        require(pixels == viewport.captureScenePixels(),"palette roundtrip changed frame");
        if (entry.presetId == "locust") require(changed != pixels,"biome palette had no visible effect");
    }
    require(battleSnapshotFingerprint(world.snapshot()) == fingerprint,"render altered battle snapshot");
    viewport.shutdown(); if (parent) DestroyWindow(parent);
}

void objectChecks(bool visual) {
    using namespace battle;
    using namespace presentation;
    const std::filesystem::path resource = "Sorted Original Files/TBL/viewer8/OTHPCK.TBL";
    const auto records = legacy3d::loadRuntimeShapeRecords(resource);
    BattleStartParams params;
    params.terrainScenarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
    params.terrainScenarioIndex = 2; params.terrainEnvironmentId = 1;
    const auto setup = decodeOriginalBattlefieldSetup(params.terrainScenarioPath,2,8,params.terrainCellSize);
    params.playerStartTransform = setup.playerStartTransform;
    BattleCombatantLaunchState enemy; enemy.roster.team = BattleTeam::Opposing;
    enemy.startTransform = {95000,0,50000,0}; params.combatantLaunchStates.push_back(enemy);
    const auto world = BattleWorld::create(params);
    const auto snapshot = world.snapshot();
    const auto before = battleSnapshotFingerprint(snapshot);
    auto scene = campaignBattleRenderSceneFromSnapshot(snapshot,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
    require(scene.valid,scene.reason);
    scene.cockpitCrosshairVisible = false;
    scene.objective.valid = true;
    scene.objective.staticModel = originalBattleObjectiveStaticModel();
    scene.objective.modelResourcePath = resource;
    scene.objective.transform = params.playerStartTransform;
    scene.objective.transform.headingRadians = .4;
    CampaignBattleRenderProjectileInstance missile;
    missile.projectileId = 1; missile.recordIndex = 1; missile.modelResourcePath = resource;
    missile.direction = {.35,.1,1}; missile.spinDegrees = 25;
    scene.projectileVisuals = {missile};
    const uint16_t factoryColors = (1u<<0) | (1u<<8) | (1u<<14);
    const uint16_t rocketColors = (1u<<4) | (1u<<7) | (1u<<8) | (1u<<12) | (1u<<14) | (1u<<15);
    CampaignBattleGlViewport viewport;
    HWND parent = nullptr;
    if (visual) {
        parent = CreateWindowExW(0,L"STATIC",L"Object color probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"object WGL initialize failed");
        viewport.setBounds(RECT{0,0,640,480});
    }
    for (const auto& biome : {"TROPIC","DESERT","ARCTIC"}) {
        legacy3d::GpuBatchOptions options; options.originalEga = palette(biome);
        scene.terrain.palettePath = std::filesystem::path("Sorted Original Files/PAL")/(std::string(biome)+".PAL");
        scene.terrain.environmentId = std::string(biome)=="TROPIC" ? 1 : (std::string(biome)=="DESERT" ? 0 : 2);
        const auto probe = probeCampaignBattleGlResources(scene);
        require(probe.valid && probe.objectiveFillEgaColorMask == factoryColors && probe.rocketFillEgaColorMask == rocketColors,
            "game resource loaders lost original factory/rocket colors: "+probe.reason);
        for (int record : {1,14}) {
            auto oldOptions = legacy3d::GpuBatchOptions{};
            oldOptions.shadeMode = record == 1 ? "ega" : "stored";
            const auto old = legacy3d::buildGpuBatch(records.at(record),oldOptions);
            const auto batch = legacy3d::buildGpuBatch(records.at(record),options);
            sameGeometry(old,batch);
            require(old.vertices != batch.vertices,"test must detect previous diagnostic color conversion");
            for (const auto& part : records.at(record).parts) for (const auto& command : part.commands)
                for (const auto& primitive : command.primitives) for (size_t component : {2u,3u}) {
                    const auto pair = options.originalEga->colorPairs.at(primitive.shadeBytes[component]);
                    require((pair&15u) == (pair>>4u),"object color requires a dither pass");
                }
            if (!visual) continue;
            auto capture = scene;
            const double x = params.playerStartTransform.x, z = params.playerStartTransform.z;
            // Controlled camera samples the actual game draw path. The rocket is
            // shown close enough to distinguish individual authored surfaces.
            capture.camera.valid = true;
            if (record == 14) {
                capture.projectileVisuals.clear();
                capture.camera.transform = {x,1400,z-10000,0};
            } else {
                capture.objective.valid = false;
                capture.camera.transform = {x,1800,z-1100,0};
                capture.projectileVisuals.front().position = {x,1800,z};
            }
            auto empty = capture; empty.objective.valid = false; empty.projectileVisuals.clear();
            require(viewport.updateScene(empty),viewport.lastError());
            const auto background = viewport.captureScenePixels();
            require(viewport.updateScene(capture),viewport.lastError());
            const auto pixels = viewport.captureScenePixels();
            require(pixels.size() == 640*480 && pixels == viewport.captureScenePixels(),"object readback not stable");
            size_t yellow = 0, red = 0, dark = 0;
            for (size_t i = 0; i < pixels.size(); ++i) if (pixels[i] != background.at(i)) {
                const uint32_t color = pixels[i]&0xffffffu;
                yellow += color==0xffff55; red += color==0xaa0000 || color==0xff5555;
                dark += color==0 || color==0x555555;
            }
            require(yellow>10 && dark>10 && (record==14 || red>10),"actual WGL object lacks source colors");
            savePpm(std::string(record==14?"factory_":"rocket_")+biome,pixels,"outputs/object_color_audit");
            // Changing protected-objective status must not tint the world model.
            capture.objective.playerProtected = !capture.objective.playerProtected;
            require(viewport.updateScene(capture),viewport.lastError());
            require(pixels == viewport.captureScenePixels(),"objective ownership tinted world model");
        }
    }
    require(battleSnapshotFingerprint(world.snapshot()) == before,"object rendering mutated battle");
    viewport.shutdown(); if (parent) DestroyWindow(parent);
}
}
int main(int argc, char** argv) {
    try {
        const bool visual = argc == 2 && std::string(argv[1]) == "--visual-gl";
        descriptorChecks(); catalogChecks(); sceneChecks(visual); objectChecks(visual);
        std::cout << "mw_mech_colors_smoke: ok checks=" << checks << " mechs=8 objects=2 palettes=3 wgl=" << (visual?"yes":"no") << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mech colors: " << e.what() << '\n'; return 1;
    }
}
