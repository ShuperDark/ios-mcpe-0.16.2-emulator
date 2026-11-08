// User-editable key bindings (controls.ini in the data folder).
#pragma once
#include "common.h"

#include <string>
#include <vector>

namespace controls {

struct Binding {
    std::string button;  // game button name, e.g. "button.jump"
    int code;            // keyboard: Windows virtual-key code; mouse: 1 left, 2 right, 3 middle
};

void init(const std::wstring& path);  // writes the default file when it does not exist
bool reload_if_changed();             // true when the file was re-read

const std::vector<Binding>& keyboard();
const std::vector<Binding>& mouse();
float mouse_sensitivity();
bool invert_mouse_y();
float scroll_speed();
bool start_fullscreen();
bool vsync();
int max_fps();  // 0 = unlimited, -1 = monitor refresh rate
bool classic_screens();  // desktop (PC) style screens instead of the pocket ones
bool is_key_for(const char* button, int vk);

// [remote]: the screen streamed to a phone's browser (remote.cpp)
struct RemoteConfig {
    bool enabled = true;
    int port = 8080;
    int resolution = 1280;  // long side of the render size while a phone is connected
    int quality = 70;       // JPEG quality
    int fps = 60;
    bool pc_audio = false;  // keep playing the sound on the PC while a phone is connected
};
RemoteConfig remote();

}  // namespace controls
