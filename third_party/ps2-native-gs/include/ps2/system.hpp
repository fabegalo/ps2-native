#pragma once
#include "ps2/cpu.hpp"
#include <cstdint>
#include <functional>
namespace ps2 {
struct GsHostOptions {bool pal{},widescreen{};};
GsHostOptions& gs_options();
std::uint64_t now();
std::uint64_t frame_count();
std::uint64_t frames_run();
std::uint64_t scheduled_at(int event);
void schedule(int event,std::uint64_t at,std::function<void()> action);
}
