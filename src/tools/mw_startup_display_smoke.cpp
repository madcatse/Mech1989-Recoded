#include "../main.cpp"
#include <iostream>

namespace {
size_t checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void ppm(const fs::path& path, const std::vector<uint32_t>& pixels, int width = 320, int height = 200) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto c : pixels) {
        const char bytes[]{static_cast<char>(c >> 16), static_cast<char>(c >> 8), static_cast<char>(c)};
        out.write(bytes, 3);
    }
    check(bool(out), "write preview");
}
// Read the production GDI paint path at physical output resolution, without
// desktop capture, window-manager resizing or an image viewer's own scaling.
struct DibSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* pixels = nullptr;
    DibSurface(int width, int height) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        pixels = static_cast<uint32_t*>(bits);
        if (dc && bitmap) previous = SelectObject(dc, bitmap);
    }
    ~DibSurface() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    DibSurface(const DibSurface&) = delete;
    DibSurface& operator=(const DibSurface&) = delete;
};
}

void runStartupDisplaySmoke() {
    using namespace mw::presentation;
    const fs::path out = "outputs/startup_display_validation";
    fs::create_directories(out);
    const auto ini = out / "test-settings.ini";
    for(bool enabled : {false,true}) {
        check(saveDisplaySettings(ini,{3,true,enabled}),"save display and control preferences together");
        const auto restored=loadDisplaySettings(ini);
        check(restored.resolution==3 && restored.fullscreen && restored.mouseLook==enabled,
            "mouselook round trip preserves the other preferences");
    }
    { std::ofstream file(ini); file << "[display]\nresolution=1920x1080\nfullscreen=no\nmouselook=yes\n"; }
    check(!loadDisplaySettings(ini).mouseLook,"legacy display-only INI and wrong section keep classic controls");
    { std::ofstream file(ini); file << "[controls]\nmouselook=maybe\n[other]\nmouselook=yes\n"; }
    check(!loadDisplaySettings(ini).mouseLook,"invalid or unrelated controls values keep the default");
    for (int i = 0; i < static_cast<int>(displayResolutions.size()); ++i) {
        for (bool fullscreen : {false, true}) {
            check(saveDisplaySettings(ini, {i, fullscreen}), "save preferences");
            const auto loaded = loadDisplaySettings(ini);
            check(loaded.resolution == i && loaded.fullscreen == fullscreen, "restart round trip");
            for (const auto size : {DisplayResolution{1280,800}, {1600,900}, {1920,1080},
                    {1920,1200}, {2560,1440}, {2560,1600}, {3840,2160}, {1914,1326},
                    {640,480}, {320,200}, {319,200}, {320,199}, {100,50}, {0,0}}) {
                const auto r = pixelDisplayRect(size.width, size.height, loaded);
                check(r.x >= 0 && r.y >= 0 && r.x + r.width <= size.width &&
                    r.y + r.height <= size.height, "canvas fits even when minimized or display shrinks");
                check(r.width % 320 == 0 && r.height % 200 == 0,
                    "integer scale on each axis, including subnative clients");
                if (r.width) check(std::abs(r.height/200 - 1.2*r.width/320) <= .5,
                    "vertical pixel scale approximates the DOS pixel aspect to the nearest integer");
            }
        }
    }
    { std::ofstream file(ini); file << "[other]\nfullscreen=yes\n[display]\nresolution=999999999999x1\nfullscreen=maybe\n"; }
    auto loaded = loadDisplaySettings(ini);
    check(loaded.resolution == 0 && !loaded.fullscreen, "corrupt/unknown values use defaults");
    check(!saveDisplaySettings(out / "absent-directory/settings.ini", {}), "unwritable settings reported");
    check(loadDisplaySettings(out / "absent-directory/settings.ini").resolution == 0, "missing file defaults");

    App app(findResourceRoot()); app.loadResources(); app.settingsPath_ = ini;
    const fs::path displayOut = "outputs/dos_display_scaling";
    fs::create_directories(displayOut);
    std::ofstream pixelReport(displayOut / "gdi-pixels.csv");
    pixelReport << "frame,width,height,scale_x,scale_y,x,y,compared_pixels,mismatches\n";
    // Explicit expectations also guard rounding at native and scale boundaries.
    struct PixelCase { int width, height, scaleX, scaleY, x, y, resolution; };
    const PixelCase pixelCases[] = {
        {1280,800,3,4,160,0,1}, {1600,900,3,4,320,50,2}, {1920,1080,4,5,320,40,3},
        {1920,1200,5,6,160,0,4}, {2560,1440,6,7,320,20,5}, {2560,1600,7,8,160,0,6},
        {3840,2160,8,10,640,80,7}, {1914,1326,5,6,157,63,0}, {641,401,2,2,0,0,0},
        {639,399,1,1,159,99,0}, {320,200,1,1,0,0,0}, {319,200,0,0,159,100,0},
        {320,199,0,0,160,99,0}, {100,50,0,0,50,25,0}
    };
    uint64_t comparedPixels = 0;
    for (bool pattern : {true, false}) {
        for (const auto c : pixelCases) {
            app.displaySettings_ = {c.resolution, false};
            if (pattern) {
                // Unique neighbouring colours expose missing, duplicated or
                // blended pixels in either axis; menu alone has large flat areas.
                for (int y=0; y<200; ++y) for (int x=0; x<320; ++x)
                    app.framebuffer_[static_cast<size_t>(y)*320+x] =
                        0xff000000u | (static_cast<uint32_t>(y)<<16) | static_cast<uint32_t>(x+1);
            } else {
                app.state_ = ScreenState::SystemMenu;
                app.systemMenuIndex_ = 3;
                app.render();
            }
            const auto rect = app.displayRectForClient(c.width,c.height);
            check(rect.x==c.x && rect.y==c.y && rect.width==320*c.scaleX && rect.height==200*c.scaleY,
                "expected DOS-corrected integer canvas and centred black bars");
            DibSurface surface(c.width,c.height);
            check(surface.dc && surface.bitmap && surface.pixels && surface.previous && surface.previous != HGDI_ERROR,
                "allocate native physical-pixel paint surface");
            const size_t count = static_cast<size_t>(c.width)*c.height;
            std::fill_n(surface.pixels, count, 0x00ff00ffu);
            app.paintFramebuffer(surface.dc,c.width,c.height);
            GdiFlush();
            size_t mismatches = 0;
            for (int y=0; y<c.height; ++y) for (int x=0; x<c.width; ++x) {
                uint32_t expected = 0;
                if (c.scaleX && c.scaleY && x>=c.x && x<c.x+320*c.scaleX && y>=c.y && y<c.y+200*c.scaleY)
                    expected = app.framebuffer_[static_cast<size_t>((y-c.y)/c.scaleY)*320+(x-c.x)/c.scaleX];
                if ((surface.pixels[static_cast<size_t>(y)*c.width+x] & 0x00ffffffu) !=
                    (expected & 0x00ffffffu)) ++mismatches;
            }
            pixelReport << (pattern ? "pattern" : "menu") << ',' << c.width << ',' << c.height << ','
                << c.scaleX << ',' << c.scaleY << ',' << c.x << ',' << c.y << ',' << count << ',' << mismatches << '\n';
            check(mismatches == 0, "every source pixel has the same integer width and height; all spare pixels are black");
            comparedPixels += count;
            if (!pattern && c.width==1920 && c.height==1080) {
                ppm(displayOut / "menu-source.ppm",app.framebuffer_);
                ppm(displayOut / "menu-1920x1080.ppm",
                    std::vector<uint32_t>(surface.pixels,surface.pixels+count),c.width,c.height);
            }
        }
    }
    pixelReport.close();
    check(bool(pixelReport), "write physical-pixel comparison report");
    app.changeState(ScreenState::Title);
    app.handleKey(VK_SPACE);
    check(app.state_ == ScreenState::Authorization, "title enters welcome");
    app.update(app.stateStartedTick_ + 60000);
    check(app.state_ == ScreenState::Authorization, "welcome remains readable without timeout");
    app.render(); ppm(out / "welcome.ppm", app.framebuffer_);
    const auto welcomePixels = app.framebuffer_;
    const auto defaultCanvas = pixelDisplayRect(kDefaultDisplayWidth,kDefaultDisplayHeight,{});
    check(kDefaultDisplayWidth==1280 && kDefaultDisplayHeight==1000 &&
        defaultCanvas.width==1280 && defaultCanvas.height==1000,
        "AUTO window restores the documented pre-resolution 1280x1000 / 4x5 mode");
    const auto exactDosCanvas = pixelDisplayRect(1600,1200,{});
    check(exactDosCanvas.width==1600 && exactDosCanvas.height==1200,
        "1600x1200 restores the documented exact DOS 5x6 mode");
    app.currentKeyDownRepeat_ = true; app.handleKey('A');
    check(app.state_ == ScreenState::Authorization, "held key cannot dismiss welcome");
    app.currentKeyDownRepeat_ = false; app.handleKey('A');
    check(app.state_ == ScreenState::CampaignMessage, "any fresh key dismisses welcome");
    app.changeState(ScreenState::Authorization); app.handleMouseClick(0, 0, false);
    check(app.state_ == ScreenState::CampaignMessage, "click dismisses welcome");

    app.drawImageFullScreenOrCentered(app.titleArchive_, 2);
    app.drawGpPanel(0,116,320,84,8); ppm(out / "panel.ppm", app.framebuffer_);
    WNDCLASSW wc{}; wc.lpfnWndProc = &App::windowProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MWStartupDisplaySmoke";
    check(RegisterClassW(&wc) != 0, "register isolated native test window");
    app.hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"MW display validation", WS_OVERLAPPEDWINDOW,
        0,0,800,600,nullptr,nullptr,wc.hInstance,&app);
    check(app.hwnd_ != nullptr, "create hidden native window");
    for (int i = 0; i < static_cast<int>(displayResolutions.size()); ++i) {
        app.displaySettings_ = {i, false}; app.applyDisplaySettings();
        RECT window{}, client{}; GetWindowRect(app.hwnd_, &window); GetClientRect(app.hwnd_, &client);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(app.hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        check(window.left >= monitor.rcWork.left && window.right <= monitor.rcWork.right &&
            window.top >= monitor.rcWork.top && window.bottom <= monitor.rcWork.bottom,
            "window stays on current monitor at all requested sizes");
        app.toggleFullscreen(); GetWindowRect(app.hwnd_, &window);
        check(EqualRect(&window, &monitor.rcMonitor), "borderless fullscreen fills monitor");
        check(!(GetWindowLongPtrW(app.hwnd_, GWL_STYLE) & WS_CAPTION), "fullscreen has no caption");
        loaded = loadDisplaySettings(ini);
        check(loaded.fullscreen && loaded.resolution == i, "fullscreen persists");
        app.toggleFullscreen();
        check((GetWindowLongPtrW(app.hwnd_, GWL_STYLE) & WS_CAPTION) == WS_CAPTION, "window frame restored");
        GetClientRect(app.hwnd_, &client);
        const auto rect = app.displayRectForClient(client.right, client.bottom);
        // The same viewport drives mouse input, GDI and the GL child bounds.
        for (const auto point : {std::pair<int,int>{0,0}, {159,99}, {319,199}}) {
            int x=-1, y=-1;
            const int scaleX = rect.width/320, scaleY = rect.height/200;
            for (const auto corner : {std::pair<int,int>{0,0}, {scaleX-1,0}, {0,scaleY-1}, {scaleX-1,scaleY-1}}) {
                const int cx = rect.x + point.first*scaleX+corner.first;
                const int cy = rect.y + point.second*scaleY+corner.second;
                check(app.clientToScreenPixel(cx,cy,x,y) && x==point.first && y==point.second,
                    "all four corners of a scaled pixel map to the same source pixel");
            }
        }
        int x=0,y=0;
        check(!app.clientToScreenPixel(rect.x-1,rect.y,x,y), "letterbox rejects clicks");
        check(!app.clientToScreenPixel(rect.x+rect.width,rect.y,x,y) &&
            !app.clientToScreenPixel(rect.x,rect.y-1,x,y) &&
            !app.clientToScreenPixel(rect.x,rect.y+rect.height,x,y), "all other bars reject clicks");
        const auto bounds = app.campaignBattleGlViewportBounds(
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            mw::battle::CampaignCockpitFamily::Light);
        check(bounds.left >= rect.x && bounds.right <= rect.x+rect.width &&
            bounds.top >= rect.y && bounds.bottom <= rect.y+rect.height, "GL child within shared viewport");
        app.renderAuthorization();
        check(app.framebuffer_ == welcomePixels, "resolution does not resample or change source pixels");
    }
    // Render all cockpit families at small, HD and 4K presentation sizes on the
    // actual WGL backend. These are hidden diagnostic buffers, not OS captures.
    CampaignBattleGlViewport viewport;
    check(viewport.initialize(wc.hInstance, app.hwnd_), "initialize WGL display test");
    viewport.setBounds({157,99,157,99});
    viewport.setVisible(true);
    RECT emptyClient{};
    const HWND glChild = GetWindow(app.hwnd_,GW_CHILD);
    check(glChild && GetClientRect(glChild,&emptyClient) && emptyClient.right==0 && emptyClient.bottom==0,
        "empty canvas has an empty GL child, not a stray one-pixel view");
    for (const std::string preset : {"jenner", "shadow_hawk", "battlemaster"}) {
        mw::battle::BattleStartParams params;
        params.playerMechPresetId = preset;
        params.terrainScenarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
        params.terrainScenarioIndex=2; params.terrainEnvironmentId=1;
        params.playerStartTransform={60000,0,30000,0}; params.fixedTickSeconds=.05;
        const auto world = mw::battle::BattleWorld::create(params);
        const auto snapshot = world.snapshot();
        const auto fingerprint = mw::battle::battleSnapshotFingerprint(snapshot);
        const auto scene = mw::battle::campaignBattleRenderSceneFromSnapshot(snapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit, "Sorted Original Files");
        check(scene.valid && viewport.updateScene(scene), "load cockpit scene for resize");
        const auto virtualRect = campaignBattleGlVirtualViewportRect(scene.mode, scene.cockpit.family);
        for (const auto size : {DisplayResolution{1280,800}, {1920,1080}, {3840,2160}}) {
            const auto display = pixelDisplayRect(size.width,size.height,{});
            const int w = virtualRect.width*display.width/320;
            const int h = virtualRect.height*display.height/200;
            check(w%virtualRect.width==0 && h%virtualRect.height==0 &&
                w/virtualRect.width==display.width/320 && h/virtualRect.height==display.height/200,
                "WGL uses the same per-axis DOS pixel scale as the surrounding frame");
            viewport.setBounds({0,0,w,h});
            RECT restoredClient{};
            check(GetClientRect(glChild,&restoredClient) && restoredClient.right==w && restoredClient.bottom==h,
                "native GL child restores exact dimensions after an empty canvas");
            const auto pixels = viewport.captureScenePixels();
            check(pixels.size() == static_cast<size_t>(w)*h, "WGL framebuffer dimensions after resize");
            check(!pixels.empty() && std::any_of(pixels.begin(),pixels.end(),
                [&](uint32_t c){return c != pixels.front();}), "WGL frame has cockpit/terrain content");
            check(mw::battle::battleSnapshotFingerprint(world.snapshot()) == fingerprint,
                "display changes do not advance battle");
        }
    }
    viewport.shutdown();
    app.changeState(ScreenState::SystemMenu); app.systemMenuIndex_ = 3;
    app.displaySettings_ = {0,false}; app.handleKey(VK_LEFT);
    check(app.displaySettings_.resolution == 7 && app.systemMenuIndex_ == 3, "left selects previous resolution");
    app.handleKey(VK_RIGHT); check(app.displaySettings_.resolution == 0, "right wraps to auto");
    app.handleSystemMenuClick(160, 120);
    check(app.displaySettings_.fullscreen && app.systemMenuIndex_ == 4, "fullscreen menu hit geometry");
    app.handleMessage(app.hwnd_, WM_SYSKEYDOWN, VK_RETURN, 1ll<<29);
    check(!app.displaySettings_.fullscreen, "Alt+Enter exits fullscreen");
    check(app.handleMessage(app.hwnd_, WM_SYSCHAR, VK_RETURN, 0)==0, "Alt+Enter character consumed");
    app.displaySettings_ = {7,false}; app.render(); ppm(out / "settings.ppm", app.framebuffer_);
    app.settingsPath_ = out / "absent-directory/settings.ini"; app.cycleResolution(1);
    check(app.displaySettingsSaveFailed_, "save failure visible in menu");
    app.render(); ppm(out / "settings-save-failure.ppm", app.framebuffer_);
    app.displaySettings_ = {6,true};
    auto gam = readFile("research/fixtures/dosbox_story/start.gam");
    gam[App::kGamOffsetDetailLevel] = 2;
    app.applyGamSaveToRuntime(gam);
    check(app.displaySettings_.resolution==6 && app.displaySettings_.fullscreen && app.detailLevel_==2,
        "DOS restore preserves independent display preferences and legacy detail byte");
    DestroyWindow(app.hwnd_); app.hwnd_ = nullptr;
    std::cout << "mw_startup_display_smoke: ok " << checks << " checks; " << comparedPixels
        << " GDI pixels; 28 physical frames; 9 WGL frames\n";
}
int main() { try { SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    runStartupDisplaySmoke(); return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
