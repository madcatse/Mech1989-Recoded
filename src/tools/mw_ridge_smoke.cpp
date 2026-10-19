#include "battle/battle_module.h"
#include "presentation/campaign_battle_gl_viewport.h"
#include "mech3d/mech_catalog.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace mw::battle;
constexpr double tau=6.28318530717958647692;
const std::filesystem::path out="outputs/ridge_validation";
int checks=0;
void check(bool v,const std::string& m) {++checks;if(!v)throw std::runtime_error(m);}
void close(double a,double b,const std::string& m) {check(std::abs(a-b)<1e-8,m+": "+std::to_string(a)+" / "+std::to_string(b));}
BattleStartParams fixture(size_t scenario=0,const std::string& mech="jenner") {
    BattleStartParams p;p.terrainScenarioPath="Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex=scenario;p.terrainEnvironmentId=0;p.playerMechPresetId=mech;p.fixedTickSeconds=.05;
    const auto base=BattleWorld::create(p).snapshot();
    std::vector<std::filesystem::path> paths;
    for(const auto& tile:base.terrain.tileNames) paths.push_back(std::filesystem::path("Sorted Original Files/WLD")/(tile+".WLD"));
    p.originalTerrainSceneCatalog=loadOriginalBattleTerrainSceneCatalog(
        "Sorted Original Files/GI/TERPCK.GI","Sorted Original Files/TBL/viewer8/TERPCK.TBL",paths);
    p.mechHitProfiles.push_back(campaignBattleCatalogHitProfile("Sorted Original Files",mech));
    p.cockpitCameraHeight=p.mechHitProfiles.front().aimOriginHeight;
    p.deterministicCombatRuntimeEnabled=p.individualWeaponRuntimeEnabled=p.originalProjectileRuntimeEnabled=true;
    p.weaponTargetPolicy=BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
    return p;
}
Transform pose(const BattleStartParams& p,int x,int z,uint16_t h=0) {
    const double s=p.terrainCellSize/512.0;
    return {(x+44160.0)*s,0,(24000.0-z)*s,std::remainder(h*tau/65536.0+tau*.5,tau)};
}
void tick(BattleWorld& w,BattleInputCommand c={},BattleReplay* r=nullptr) {
    c.tickIndex=w.tickIndex();c.entityId=w.playerEntityId();if(r)r->commands.push_back(c);w.enqueueInput(c);w.tick();
}
struct Sample {int x,z;uint16_t heading;BattleOriginalTerrainSceneQueryDiagnostic q;};
std::vector<Sample> samples(const BattleStartParams& p) {
    std::vector<Sample> result;std::set<std::pair<size_t,int>> faces;
    for(const auto& obj:p.originalTerrainSceneCatalog.objects) {
        if(obj.recordIndex<26 || obj.recordIndex>29)continue;
        for(int dx=-8000;dx<=8000;dx+=500)for(int dz=-8000;dz<=8000;dz+=500) {
            auto q=battleOriginalTerrainSceneQuery(p.originalTerrainSceneCatalog,obj.rawX+dx,obj.rawZ+dz,0,0,0);
            if(!q.hit || q.selectedResultByte!=0 || q.worldHeight<=10 || q.contactCorrection.desiredByte0==0)continue;
            if(faces.insert({q.selectedSceneObjectIndex,q.selectedSubrecordIndex}).second)
                result.push_back({obj.rawX+dx,obj.rawZ+dz,0,q});
        }
    }
    return result;
}
void exportSamples(const BattleStartParams& p,const std::vector<Sample>& rows) {
    std::ofstream f(out/"scene-samples.csv");f<<"object,record,face,x,z,height,pitch_byte\n";
    for(const auto& s:rows) f<<s.q.selectedSceneObjectIndex<<','<<s.q.selectedRecordIndex<<','<<int(s.q.selectedSubrecordIndex)<<','<<s.x<<','<<s.z<<','<<s.q.worldHeight<<','<<int(s.q.contactCorrection.desiredByte0)<<'\n';
    std::ofstream objects(out/"scene-objects.csv");objects<<"record,x,z,y\n";
    for(const auto& o:p.originalTerrainSceneCatalog.objects)objects<<o.recordIndex<<','<<o.rawX<<','<<o.rawZ<<','<<o.rawY<<'\n';
}
void stationaryAndAim() {
    for(const auto& mech:mw::mech3d::mechCatalogEntries()) {
        auto p=fixture(0,mech.presetId);const auto rows=samples(p);check(!rows.empty(),"real scene has ridge faces");
        for(int side=0;side<2;++side) {
            const auto& s=rows.front();const uint16_t heading=side?0x8000:0;
            p.playerStartTransform=pose(p,s.x,s.z,heading);
            const auto q=battleOriginalTerrainSceneQuery(p.originalTerrainSceneCatalog,s.x,s.z,0,0,uint8_t(heading>>8));
            auto w=BattleWorld::create(p);const double height=q.worldHeight*p.terrainCellSize/512.0;
            close(w.snapshot().combatants[0].transform.y,height,"spawn rests on ridge");
            BattleReplay replay;
            for(int i=0;i<100;++i) {
                const auto before=w.snapshot();tick(w,{},&replay);const auto now=w.snapshot();
                check(!now.combatants[0].airborne,"standing ridge is not flight");
                check(std::abs(now.combatants[0].ridge.pitch-before.combatants[0].ridge.pitch)<=256,"bounded source pitch step");
                const auto scene=campaignBattleInterpolatedRenderSceneFromSnapshots(before,now,.5,
                    CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
                close(scene.camera.ridgePitchRadians,(before.camera.ridgePitchRadians+now.camera.ridgePitchRadians)*.5,"slope interpolates between ticks");
            }
            const int16_t target=static_cast<int16_t>(uint16_t(q.contactCorrection.desiredByte0)<<8);
            check(w.snapshot().combatants[0].ridge.pitch==target,"original byte target reached");
            close(w.snapshot().combatants[0].transform.y,height,"standing does not sink");
            check(battleSnapshotFingerprint(runBattleReplay(p,replay,w.tickIndex()))==battleSnapshotFingerprint(w.snapshot()),"ridge replay deterministic");
            for(int zoom=1;zoom<=3;++zoom) {
                // Fresh weapons for each shot; retain a settled ridge pose.
                auto gun=BattleWorld::create(p);gun.runTicks(100);
                BattleInputCommand c;c.mouseLookEnabled=true;c.mouseLookZoomLevel=zoom;
                c.mouseLookYawDeltaRadians=.2;c.mouseLookPitchDeltaRadians=.07;c.fireWeapon=true;
                tick(gun,c);const auto state=gun.snapshot();const auto& shot=state.lastShot;
                check(shot.valid,"ridge mouse shot accepted");
                const auto& profile=p.mechHitProfiles.front();const double half=profile.cockpitViewportHeight*.5;
                const double t=std::tan(profile.verticalFieldOfViewDegrees*tau/720.0)/zoom;
                const double sx=(profile.cockpitViewportWidth*.5-profile.neutralCrosshairX)/half*t;
                const double sy=(half-profile.neutralCrosshairY)/half*t;
                const double h=state.camera.transform.headingRadians;
                const double pitch=state.camera.ridgePitchRadians-state.camera.mouseLookPitchRadians;
                const double f=std::cos(pitch)+sy*std::sin(pitch);
                const double x=std::sin(h)*f+std::cos(h)*sx,y=-std::sin(pitch)+sy*std::cos(pitch),z=std::cos(h)*f-std::sin(h)*sx;
                const double length=std::sqrt(x*x+y*y+z*z);
                close(shot.aimDirection.x,x/length,"ridge shot X aligned");close(shot.aimDirection.y,y/length,"ridge shot Y aligned");close(shot.aimDirection.z,z/length,"ridge shot Z aligned");
                close(shot.aimOrigin.x,state.camera.transform.x,"tilted eye X matches shot");
                close(shot.aimOrigin.y,state.camera.transform.y,"tilted eye Y matches shot");
                c={};c.mouseLookEnabled=false;tick(gun,c);
                close(gun.snapshot().camera.ridgePitchRadians,state.camera.ridgePitchRadians,"classic toggle retains slope");
            }
        }
    }
}
void traverseAndJump() {
    auto p=fixture(12);p.playerStartTransform=pose(p,16210,-11000);
    p.maxForwardSpeed=1500;p.acceleration=12000;
    auto w=BattleWorld::create(p);BattleReplay replay;
    bool uphill=false,downhill=false,upPitch=false,downPitch=false;double highest=0,prior=0;
    std::ofstream f(out/"cpp-crossing.csv");f<<"tick,x,z,height,pitch,camera_pitch\n";
    for(int i=0;i<500;++i) {
        BattleInputCommand c;c.throttle=1;tick(w,c,&replay);auto s=w.snapshot();const auto& a=s.combatants[0];
        check(!a.airborne,"crossing ridge never becomes an unintended jump");
        highest=std::max(highest,a.transform.y);uphill=uphill||a.transform.y>prior;downhill=downhill||a.transform.y<prior;
        upPitch=upPitch||a.ridge.pitch>0;downPitch=downPitch||a.ridge.pitch<0;prior=a.transform.y;
        f<<s.tickIndex<<','<<a.transform.x<<','<<a.transform.z<<','<<a.transform.y<<','<<a.ridge.pitch<<','<<s.camera.ridgePitchRadians<<'\n';
        if(a.transform.z<19000)break;
    }
    check(highest>100 && uphill && downhill && upPitch && downPitch,"complete ascent, crest, descent and both pitch signs");
    check(battleSnapshotFingerprint(runBattleReplay(p,replay,w.tickIndex()))==battleSnapshotFingerprint(w.snapshot()),"crossing replay exact");
    BattleInputCommand stop;stop.throttle=0;for(int i=0;i<100;++i)tick(w,stop);
    close(w.snapshot().combatants[0].ridge.groundHeight,0,"left ridge for flat ground");
    close(w.snapshot().camera.ridgePitchRadians,0,"camera returns level on flat ground");
    p=fixture(12);p.playerStartTransform=pose(p,14710,-2714);
    p.jumpJetInitialImpulse=150;p.jumpJetInitialImpulseSeconds=.1;
    w=BattleWorld::create(p);w.runTicks(100);const auto grounded=w.snapshot();
    BattleInputCommand jump;jump.jumpJetToggle=true;tick(w,jump);
    check(w.snapshot().combatants[0].airborne,"jump starts on raised ridge");
    bool rose=false,landed=false;
    for(int i=0;i<200;++i) {
        tick(w);const auto s=w.snapshot();rose=rose||s.combatants[0].transform.y>grounded.combatants[0].transform.y;
        if(!s.combatants[0].airborne){landed=true;break;}
    }
    check(rose && landed,"jump rises and lands on ridge");
    close(w.snapshot().combatants[0].transform.y,grounded.combatants[0].transform.y,"landing uses ridge height, not zero");
    check(!w.snapshot().combatants[0].hardLanding,"soft ridge landing is not a false fall");
    p=fixture(12);p.playerStartTransform=pose(p,16210,-6818);w=BattleWorld::create(p);w.runTicks(100);
    const auto beforeReverse=w.snapshot();BattleInputCommand reverse;reverse.throttle=-1;
    for(int i=0;i<20;++i)tick(w,reverse);
    check(w.snapshot().combatants[0].transform.y<beforeReverse.combatants[0].transform.y,"reverse descends the facing-uphill slope");
    check(w.snapshot().combatants[0].ridge.pitch==beforeReverse.combatants[0].ridge.pitch,"slope pitch follows heading, not throttle sign");
    BattleInputCommand fire;fire.throttle=-1;fire.mouseLookEnabled=true;fire.mouseLookPitchDeltaRadians=.07;fire.fireWeapon=true;
    tick(w,fire);const auto moving=w.snapshot();
    const auto scene=campaignBattleRenderSceneFromSnapshot(moving,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
    check(moving.lastShot.valid,"moving ridge shot accepted");
    close(moving.lastShot.aimOrigin.y,scene.camera.transform.y+scene.cockpitCameraBobOffsetY,"walking bob and slope use the same weapon eye");
    std::vector<std::filesystem::path> paths;
    for(const auto& tile:moving.terrain.tileNames)paths.push_back(std::filesystem::path("Sorted Original Files/WLD")/(tile+".WLD"));
    p.terrainCollisionObstacles=loadOriginalBattleTerrainCollisionObstacles("Sorted Original Files/TBL/viewer8/TERPCK.TBL",paths,0,173*p.terrainCellSize,0,93*p.terrainCellSize,p.terrainCellSize);
    p.deterministicCollisionRuntimeEnabled=true;
    const auto& mountain=p.terrainCollisionObstacles.entries.front();
    p.playerStartTransform={mountain.centerX+mountain.halfExtentX+1000,0,mountain.centerZ,-tau*.25};
    p.maxForwardSpeed=2000;p.acceleration=10000;w=BattleWorld::create(p);
    BattleInputCommand forward;forward.throttle=1;for(int i=0;i<100;++i)tick(w,forward);
    check(w.snapshot().combatants[0].collisionCount>0,"high mountains remain blocking");
}
void dosComparison() {
    std::ifstream f("research/fixtures/dosbox_ridge/stable-cases.csv");
    check(bool(f),"original DOSBox ridge fixture exists");std::string line;std::getline(f,line);int rows=0;
    auto p=fixture(12);
    while(std::getline(f,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);
        int x,z,heading,height,pitch,control,record,face;in>>x>>z>>heading>>height>>pitch>>control>>record>>face;
        check(bool(in),"DOS comparison row parsed");++rows;
        const auto q=battleOriginalTerrainSceneQuery(p.originalTerrainSceneCatalog,x,z,uint8_t(uint16_t(pitch)>>8),0,uint8_t(heading>>8));
        check((q.hit?int(q.selectedRecordIndex):-1)==record,"same original surface record");
        check((q.hit?int(q.selectedSubrecordIndex):-1)==face,"same original surface face");
        check((q.hit?q.worldHeight:0)==height,"GI height exactly matches live DOSBox");
        check((q.hit?q.contactCorrection.controlWord30:0)==control,"settled correction matches DOSBox");
        p.playerStartTransform=pose(p,x,z,uint16_t(heading));auto w=BattleWorld::create(p);w.runTicks(100);
        const auto s=w.snapshot();check(s.combatants[0].ridge.pitch==pitch,"body pitch exactly matches DOSBox");
        close(s.combatants[0].transform.y,height*p.terrainCellSize/512.0,"DOS world height conversion");
        close(s.camera.ridgePitchRadians,-pitch*tau/65536.0,"DOS camera sign conversion");
    }
    check(rows==6,"six independent stable DOS observations");
    std::set<int> records;
    for(const auto& s:samples(p)) {
        records.insert(s.q.selectedRecordIndex);p.playerStartTransform=pose(p,s.x,s.z);
        auto w=BattleWorld::create(p);w.runTicks(100);const auto a=w.snapshot().combatants[0];
        check(!a.airborne,"all original ridge faces are grounded");
        close(a.transform.y,s.q.worldHeight*p.terrainCellSize/512.0,"all ridge face heights");
    }
    check(records==std::set<int>{26,27,28,29},"all four original ridge models covered");
}
void renderChecks() {
    using mw::presentation::CampaignBattleGlViewport;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MWRidgeSmoke";
    check(RegisterClassW(&wc)!=0,"register ridge preview window");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Ridge",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,wc.hInstance,nullptr);
    check(window!=nullptr,"create hidden ridge WGL target");
    CampaignBattleGlViewport viewport;check(viewport.initialize(wc.hInstance,window),"initialize ridge WGL");
    for(const std::string mech:{"jenner","shadow_hawk","battlemaster"})for(int mode=0;mode<3;++mode) {
        auto p=fixture(12,mech);p.playerStartTransform=pose(p,16210,mode==0?-6818:-4360);
        auto w=BattleWorld::create(p);w.runTicks(100);
        if(mode==2){BattleInputCommand c;c.mouseLookEnabled=true;c.mouseLookPitchDeltaRadians=.07;c.mouseLookYawDeltaRadians=.2;tick(w,c);}
        const auto scene=campaignBattleRenderSceneFromSnapshot(w.snapshot(),CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
        check(viewport.updateScene(scene),"upload ridge WGL scene");
        const auto rect=mw::presentation::campaignBattleGlVirtualViewportRect(scene.mode,scene.cockpit.family);const int width=rect.width*4,height=rect.height*5;
        viewport.setBounds({0,0,width,height});const auto pixels=viewport.captureScenePixels();
        check(pixels.size()==size_t(width*height),"read actual ridge WGL frame");
        std::ofstream image(out/(mech+"-"+std::to_string(mode)+".ppm"),std::ios::binary);image<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(auto color:pixels){const char rgb[]{char(color>>16),char(color>>8),char(color)};image.write(rgb,3);}
        check(bool(image),"save ridge WGL frame");
    }
    viewport.shutdown();DestroyWindow(window);
}
}
int main(int argc,char**) {try {
    std::filesystem::create_directories(out);
    auto p=fixture(12);const auto rows=samples(p);exportSamples(p,rows);
    if(argc>1){
        std::cout<<"DOS comparison scenario 12: "<<rows.size()<<" ridge faces exported\n";
        return 0;
    }
    stationaryAndAim();traverseAndJump();dosComparison();renderChecks();
    std::cout<<"mw_ridge_smoke: ok "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
