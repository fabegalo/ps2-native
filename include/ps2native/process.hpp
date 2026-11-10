#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native {
// Runs `executable` with UTF-8 `arguments` in `working_directory`, writing its
// standard output and error to `log`. While it runs, `observer` receives new
// log text; returning false terminates the process. Returns the exit code.
using ProcessObserver = std::function<bool(std::string_view)>;
int run_process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory, const std::filesystem::path& log,
                const ProcessObserver& observer = {});
} // namespace ps2native
