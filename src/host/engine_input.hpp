#pragma once
#include "ps2native/engine/console.hpp"
#include <SDL3/SDL.h>
#include <vector>
class EngineInput {
public:
    EngineInput(ps2native::engine::Console&,bool window,const std::filesystem::path& script={});
    ~EngineInput();
    void poll();
private:
    ps2native::engine::Console& console_;
    SDL_Window* window_{};
    SDL_Gamepad* devices_[2]{};
    std::vector<SDL_GamepadBinding> bindings_[2];
    SDL_Joystick* virtual_{};SDL_JoystickID virtual_id_{};
    struct Event{std::uint64_t frame;std::uint16_t buttons;};
    std::vector<Event> script_;std::size_t next_{};
    void* metal_view_{};
    std::uint64_t sample_wall_{},sample_ticks_{},sample_frames_{};
    bool attached_{};
};
