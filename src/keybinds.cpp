#include "keybinds.h"

#include "settings.h"

#include <SDL.h>
#include <windows.h>

#include <cstdlib>
#include <sstream>

namespace {
constexpr const char *kSettingNames[] = {"key_fullscreen", "key_exit_fullscreen", "key_recording",
                                         "key_screenshot", "key_settings"};
constexpr SDL_Scancode kDefaultKeys[] = {SDL_SCANCODE_F11, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_R,
                                         SDL_SCANCODE_S, SDL_SCANCODE_COMMA};
constexpr uint8_t kDefaultModifiers[] = {0, 0, 0, 0, KeybindCtrl};

uint8_t modifier_mask(SDL_Keymod mod) {
    uint8_t result = 0;
    if (mod & KMOD_CTRL) result |= KeybindCtrl;
    if (mod & KMOD_SHIFT) result |= KeybindShift;
    if (mod & KMOD_ALT) result |= KeybindAlt;
    return result;
}
} // namespace

const char *keybind_action_name(KeybindAction action) {
    switch (action) {
    case KeybindAction::Fullscreen: return "Full screen";
    case KeybindAction::ExitFullscreen: return "Leave full screen";
    case KeybindAction::Recording: return "Start/stop recording";
    case KeybindAction::Screenshot: return "Save screenshot";
    case KeybindAction::Settings: return "Open Settings";
    case KeybindAction::Count: break;
    }
    return "Unknown action";
}

KeybindSet default_keybinds() {
    KeybindSet result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = {kDefaultKeys[i], kDefaultModifiers[i]};
    return result;
}

KeybindSet load_keybinds() {
    KeybindSet result = default_keybinds();
    for (size_t i = 0; i < result.size(); ++i) {
        std::string stored = load_setting(kSettingNames[i]);
        if (stored.empty()) continue;
        unsigned int key = 0, modifiers = 0;
        char comma = 0;
        std::istringstream input(stored);
        if ((input >> key >> comma >> modifiers) && comma == ',' && key > SDL_SCANCODE_UNKNOWN &&
            key < SDL_NUM_SCANCODES && modifiers <= (KeybindCtrl | KeybindShift | KeybindAlt))
            result[i] = {static_cast<SDL_Scancode>(key), static_cast<uint8_t>(modifiers)};
    }
    return result;
}

void save_keybinds(const KeybindSet &keybinds) {
    for (size_t i = 0; i < keybinds.size(); ++i) {
        const Keybind &binding = keybinds[i];
        save_setting(kSettingNames[i], std::to_string(static_cast<int>(binding.key)) + "," +
                                           std::to_string(binding.modifiers));
    }
}

bool same_keybind(const Keybind &a, const Keybind &b) { return a.key == b.key && a.modifiers == b.modifiers; }

bool keybind_matches(const Keybind &binding, SDL_Scancode key, uint16_t modifiers) {
    return binding.key == key && binding.modifiers == modifier_mask(static_cast<SDL_Keymod>(modifiers));
}

std::string keybind_label(const Keybind &binding) {
    std::string label;
    if (binding.modifiers & KeybindCtrl) label += "Ctrl+";
    if (binding.modifiers & KeybindShift) label += "Shift+";
    if (binding.modifiers & KeybindAlt) label += "Alt+";
    SDL_Keycode key = SDL_GetKeyFromScancode(binding.key);
    const char *name = SDL_GetKeyName(key);
    label += name && *name ? name : "Unknown key";
    return label;
}

Keybind keybind_from_virtual_key(unsigned int virtual_key, uint8_t modifiers) {
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    if (virtual_key >= 'A' && virtual_key <= 'Z')
        key = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (virtual_key - 'A'));
    else if (virtual_key >= '0' && virtual_key <= '9')
        key = virtual_key == '0' ? SDL_SCANCODE_0
                                 : static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (virtual_key - '1'));
    else if (virtual_key >= VK_F1 && virtual_key <= VK_F12)
        key = static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (virtual_key - VK_F1));
    else {
        switch (virtual_key) {
        case VK_OEM_COMMA: key = SDL_SCANCODE_COMMA; break;
        case VK_OEM_PERIOD: key = SDL_SCANCODE_PERIOD; break;
        case VK_SPACE: key = SDL_SCANCODE_SPACE; break;
        case VK_ESCAPE: key = SDL_SCANCODE_ESCAPE; break;
        case VK_UP: key = SDL_SCANCODE_UP; break;
        case VK_DOWN: key = SDL_SCANCODE_DOWN; break;
        case VK_LEFT: key = SDL_SCANCODE_LEFT; break;
        case VK_RIGHT: key = SDL_SCANCODE_RIGHT; break;
        default: break;
        }
    }
    return {key, modifiers};
}
