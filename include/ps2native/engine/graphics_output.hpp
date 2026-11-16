#pragma once
#include <filesystem>
namespace ps2native::engine {
class Console;
void capture_graphics(Console&,const std::filesystem::path&);
void present_graphics(Console&,unsigned milliseconds);
}
