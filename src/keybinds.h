#pragma once

#include <SDL_scancode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

enum class KeybindAction : size_t { Fullscreen, ExitFullscreen, Recording, Screenshot, Settings, Count };

enum KeybindModifier : uint8_t { KeybindCtrl = 1, KeybindShift = 2, KeybindAlt = 4 };

struct Keybind {
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    uint8_t modifiers = 0;
};

using KeybindSet = std::array<Keybind, static_cast<size_t>(KeybindAction::Count)>;

KeybindSet default_keybinds();
KeybindSet load_keybinds();
void save_keybinds(const KeybindSet &keybinds);
bool same_keybind(const Keybind &a, const Keybind &b);
bool keybind_matches(const Keybind &binding, SDL_Scancode key, uint16_t sdl_modifiers);
std::string keybind_label(const Keybind &binding);
Keybind keybind_from_virtual_key(unsigned int virtual_key, uint8_t modifiers);
const char *keybind_action_name(KeybindAction action);
