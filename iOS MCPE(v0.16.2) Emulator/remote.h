// Shows the game in a phone's browser and takes its touches / keyboard as input.
//
// An HTTP + WebSocket server ([remote] in controls.ini, default port 8080) serves
// remote.html (next to the exe). While a phone is connected the game renders at the
// phone's aspect ratio, uses the touch (pocket) screens, and its frames are sent to the
// phone as JPEG; touches come back as multi-touch pointer events, and text typed with
// the phone's keyboard goes to the game's active text field.
#pragma once
#include "common.h"

#include <string>

namespace remote {

void start();       // starts the server when [remote] enabled = true
bool connected();   // a phone is connected (touch UI, PC mouse ignored)
// Called by window::present with the game's frame bound as GL_READ_FRAMEBUFFER.
void capture(int w, int h);
void set_text_input(bool on);  // the game wants / no longer wants keyboard input
std::string url();             // e.g. "http://192.168.1.10:8080" (empty when not running)

// Game audio (interleaved 16-bit stereo) for the phone. Returns true when the PC should
// stay silent ([remote] pc_audio = false while a phone is connected).
bool audio(const s16* samples, int frames, int rate);

// Image picker for custom skins: the connected phone's photo / file picker, otherwise a
// Windows file dialog. poll: 0 pending, 1 done (*len bytes ready for take), -1 cancelled.
void pick_image_begin();
int pick_image_poll(size_t* len);
void pick_image_take(void* out);

}  // namespace remote
