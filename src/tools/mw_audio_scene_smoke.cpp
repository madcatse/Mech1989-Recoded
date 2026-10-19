#include "../main.cpp"
#include <iostream>

void runAudioSceneSmoke() {
    using Track = mw::audio::MusicTrack;
    App app(findResourceRoot());
    app.loadResources();
    app.applyGamSaveToRuntime(readFile("research/fixtures/dosbox_story/start.gam"));
    app.clearStoryScene();
    size_t checks = 0;
    auto expect = [&](Track track) {
        ++checks;
        if (app.music_.requestedTrack() != track) throw std::runtime_error("Audio scene cue differs");
    };
    const auto persistentBefore = app.captureCampaignPersistentState().fingerprint;
    app.changeState(ScreenState::IntroText); expect(Track::Theme);
    app.changeState(ScreenState::Title); expect(Track::Theme);
    app.changeState(ScreenState::Authorization); expect(Track::None);
    app.changeState(ScreenState::MainMenu); expect(Track::None);
    app.soundEnabled_ = true;
    app.changeState(ScreenState::BarMenu); expect(Track::Bar);
    app.beginStory({app.makeStoryPage(App::kStoryQuietBarDrink)}, StoryBackdrop::Bar, ScreenState::BarMenu);
    expect(Track::Bar);
    app.clearStoryScene();
    app.changeState(ScreenState::BarMenu); expect(Track::Bar);
    app.changeState(ScreenState::MainMenu); expect(Track::None);
    app.soundEnabled_ = false;
    app.changeState(ScreenState::BarMenu); expect(Track::None);
    app.soundEnabled_ = true;
    app.changeState(ScreenState::TravelAnimation); expect(Track::None);
    app.changeState(ScreenState::MissionBattleStub); expect(Track::None);
    app.missionDebriefOutcome_ = MissionOutcome::Victory;
    app.changeState(ScreenState::MissionDebrief); expect(Track::Success);
    app.changeState(ScreenState::MissionDebrief, false); expect(Track::None);
    app.missionDebriefOutcome_ = MissionOutcome::Defeat;
    app.changeState(ScreenState::MissionDebrief); expect(Track::None);
    app.missionDebriefOutcome_ = MissionOutcome::Death;
    app.changeState(ScreenState::MissionDebrief); expect(Track::Death);
    app.changeState(ScreenState::MainMenu); expect(Track::None);
    app.beginStory({app.makeStoryPage(App::kStoryAndersMoonArrest)}, StoryBackdrop::Campaign,
        ScreenState::MainMenu, App::StoryAction::GameOver);
    expect(Track::Death);
    app.clearStoryScene();
    app.changeState(ScreenState::CampaignEnding); expect(Track::Ending);
    app.soundEnabled_ = false;
    app.changeState(ScreenState::CampaignEnding); expect(Track::None);
    app.changeState(ScreenState::MissionDebrief); expect(Track::None);
    app.soundEnabled_ = true;
    app.changeState(ScreenState::MainMenu); expect(Track::None);
    if (app.captureCampaignPersistentState().fingerprint != persistentBefore)
        throw std::runtime_error("Audio transitions changed persistent campaign state");
    ++checks;
    std::cout << "mw_audio_scene_smoke: ok " << checks << " checks; no output device opened\n";
}
int main() {
    try { runAudioSceneSmoke(); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
