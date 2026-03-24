# Audio Playback Implementation (Solution 5 — miniaudio)

## Plan
Using miniaudio single-header library to replace hand-rolled WASAPI AudioPlayer.
Key fix: miniaudio callback thread is independent of main thread; large ring buffer decouples them.

## Tasks
- [ ] Create src/AudioPlayer.h
- [ ] Create src/AudioPlayer.cpp
- [ ] Modify CMakeLists.txt — add miniaudio FetchContent + AudioPlayer.cpp + ole32/winmm links
- [ ] Modify src/Common.h — add audioVolume/muteAudio to AppConfig
- [ ] Modify src/ConfigManager.cpp — serialise audioVolume/muteAudio
- [ ] Modify src/Application.h — add AudioPlayer member + SetAudioVolume/SetAudioMute
- [ ] Modify src/Application.cpp — lifecycle + ProcessFrame drain + flush on seek/stop/pause/open
- [ ] Modify src/UIManager.cpp — mute button + volume slider in transport bar
