// Portable release integration: invoked in a disposable folder, never Original.
#include "../main.cpp"
#include "audio/pc_speaker.h"
#include <iostream>

namespace {
size_t checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void ppm(const fs::path& path, const std::vector<uint32_t>& pixels, int w, int h) {
    check(pixels.size() == size_t(w)*h, "framebuffer dimensions");
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (auto p : pixels) {
        const char bytes[]{char(p>>16),char(p>>8),char(p)};
        out.write(bytes,3);
    }
    check(bool(out), "write release preview");
}
}

void runReleaseSmoke(const fs::path& expectedRoot, bool expectMusic) {
    const auto exe = executableDirectory();
    check(fs::is_regular_file(exe / "release-test-only.marker"), "disposable install marker required");
    check(fs::equivalent(findResourceRoot(), expectedRoot), "portable resources win over unrelated working directory");
    App app(findResourceRoot());
    const HWND inheritedConsole=GetConsoleWindow();
    check(app.initialize(GetModuleHandleW(nullptr), SW_HIDE), "hidden application startup");
    check(GetConsoleWindow()==inheritedConsole,"release startup creates no diagnostics console window");
    check(app.status_.find(L"Resource load failed") == std::wstring::npos &&
        !app.planets_.empty() && !app.titleArchive_.images.empty() &&
        !app.endingArchive_.images.empty(), "campaign and ending assets load from copied DOS data");
    check(GetClassLongPtrW(app.hwnd_,GCLP_HICON)!=0, "window class icon loaded from PE resource");
    check(SendMessageW(app.hwnd_,WM_GETICON,ICON_SMALL,0)!=0, "small window icon loaded");
    check(fs::equivalent(app.settingsPath_.parent_path(),exe), "settings stay inside portable install");
    const auto audioStatus = app.music_.status();
    check((audioStatus.find("Munt ")==0)==expectMusic, "optional music dependency behavior");
    check(mw::audio::SpeakerBank::load(expectedRoot/"BTECHSND.SND").bytes.size()>0, "speaker bank loads independently");
    std::cout << "Audio: " << audioStatus << '\n';

    app.changeState(ScreenState::Title); app.render();
    ppm(exe/"title.ppm",app.framebuffer_,320,200);
    app.handleKey(VK_SPACE);
    check(app.state_==ScreenState::Authorization,"welcome scene after title");
    app.render(); ppm(exe/"welcome.ppm",app.framebuffer_,320,200);
    app.handleKey(VK_SPACE);
    app.changeState(ScreenState::MainMenu);
    app.debugConsoleInput_=L"MoNeY"; app.executeDebugConsoleCommand();
    check(app.playerWealth_==10000000,"release console and mixed-case cheats work");
    app.debugConsoleInput_=L"mouselook"; app.executeDebugConsoleCommand();
    check(app.mouseLookEnabled_ && mw::presentation::loadDisplaySettings(app.settingsPath_).mouseLook,
        "mouse-look persists in the portable settings file");
    const auto save = app.saveGameDirectory()/"RELEASE.GAM";
    check(app.saveCurrentGame(save),"save campaign to portable resource directory");
    check(fs::exists(save.wstring()+L".mwstate"),"exact-state companion saved");
    app.playerWealth_=7;
    app.loadGameFromFile(save);
    check(app.playerWealth_==10000000 && app.status_.find(L"Load failed")==std::wstring::npos,
        "restore campaign from portable save pair");
    app.changeState(ScreenState::SystemMenu); app.render();
    ppm(exe/"menu.ppm",app.framebuffer_,320,200);

    // Exercise the same battle entry and resource lookup used by the application.
    app.cheatBattleTest();
    check(app.campaignBattleRuntime_.active && app.campaignBattleRuntime_.world.has_value(),
        "battle can start with unsorted DOS assets only");
    check(fs::equivalent(app.campaignBattleRuntime_.renderAssetRoot,expectedRoot),
        "battle is not borrowing sorted development assets");
    auto& world=*app.campaignBattleRuntime_.world;
    mw::battle::BattleInputCommand look;
    look.entityId=world.playerEntityId(); look.mouseLookEnabled=true;
    look.mouseLookYawDeltaRadians=.1; look.mouseLookPitchDeltaRadians=.05;
    world.enqueueInput(look); world.tick();
    const auto snapshot=world.snapshot();
    const auto scene=mw::battle::campaignBattleRenderSceneFromSnapshot(snapshot,
        mw::battle::CampaignBattlePresentationMode::Cockpit,expectedRoot);
    mw::presentation::CampaignBattleGlViewport viewport;
    check(viewport.initialize(GetModuleHandleW(nullptr),app.hwnd_),"portable WGL initialization");
    check(scene.valid && viewport.updateScene(scene),"portable WGL scene and cockpit assets");
    viewport.setBounds({0,0,320,200}); viewport.setVisible(true);
    const auto pixels=viewport.captureScenePixels();
    check(!pixels.empty() && std::any_of(pixels.begin(),pixels.end(),
        [&](auto p){return p!=pixels.front();}),"portable WGL frame contains rendered geometry");
    ppm(exe/"battle.ppm",pixels,320,200);
    // Documentation frame: compose the production cockpit backdrop/HUD and
    // WGL child at their actual 320x200 game coordinates, without desktop UI.
    const auto rect=mw::presentation::campaignBattleGlVirtualViewportRect(scene.mode,scene.cockpit.family);
    viewport.setBounds({0,0,rect.width,rect.height});
    const auto cockpitPixels=viewport.captureScenePixels();
    check(rect.x>=0 && rect.y>=0 && rect.x+rect.width<=320 && rect.y+rect.height<=200 &&
        cockpitPixels.size()==size_t(rect.width)*rect.height,"native cockpit viewport fits the complete frame");
    const auto presentation=mw::battle::campaignBattlePresentationPackageFromSnapshot(snapshot,scene.mode);
    const bool backdropLoaded=app.ensureCampaignCockpitBackdrop(scene);
    check(backdropLoaded,
        ("portable cockpit backdrop: "+app.campaignCockpitBackdrop_.reason).c_str());
    const bool widgetsLoaded=app.ensureCampaignCockpitDynamicLayers(scene);
    check(widgetsLoaded,
        ("portable cockpit widgets: "+app.campaignCockpitDynamicLayers_.reason).c_str());
    app.renderCampaignBattleCockpitPresentation(snapshot,presentation,scene);
    for(int y=0;y<rect.height;++y)
        std::copy_n(cockpitPixels.begin()+y*rect.width,rect.width,
            app.framebuffer_.begin()+(rect.y+y)*320+rect.x);
    ppm(exe/"cockpit.ppm",app.framebuffer_,320,200);
    viewport.shutdown();
    app.music_.play(mw::audio::MusicTrack::None); app.speaker_.stop();
    DestroyWindow(app.hwnd_); app.hwnd_=nullptr;
    std::cout << "mw_release_smoke: ok " << checks << " checks; title/welcome/menu/battle; save/restore\n";
}

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: mw_release_smoke resource-directory music|silent");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        runReleaseSmoke(fs::path(argv[1]),std::wstring(argv[2])==L"music");
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
