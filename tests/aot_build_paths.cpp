// Compile and execute the original generated AOT at real path depths, then
// verify incremental source/header rebuilds and unchanged builds under Ninja.
#include "ps2native/core.hpp"
#include "boot_checks.hpp"
#include "io_process.hpp"
#include <iostream>

namespace {
using namespace ps2native;
using boot_checks::require;
using boot_checks::rejects;
namespace fs = std::filesystem;
std::string text(const fs::path& p) { const auto b = read_file(p); return {b.begin(), b.end()}; }
fs::path from_utf8(const std::string& value) { return fs::path(std::u8string(value.begin(), value.end())); }
std::string bracket(const fs::path& value) {
    const auto u8 = value.generic_u8string(); const std::string s(u8.begin(), u8.end());
    std::string eq;
    while (s.find("]" + eq + "]") != std::string::npos) eq += '=';
    return "[" + eq + "[" + s + "]" + eq + "]";
}
fs::path option(const char* prefix, const fs::path& value) { auto p = fs::path(prefix); p += value.native(); return p; }
void check_alias(const fs::path& root) {
    const auto original = root / "alias-original";
    const auto alias = root / "alias-reference";
    const auto other = root / "alias-other";
    ps2native::create_directories(original);
    ps2native::create_directories(other);
    (void)write_text_if_changed(original / "marker.txt", "original");
    ensure_directory_alias(alias, original);
    ensure_directory_alias(alias, original);
    require(files_equivalent(alias / "marker.txt", original / "marker.txt"), "alias is the same file, not a copy");
    rejects([&]{ ensure_directory_alias(alias, other); }, "different target");
    rejects([&]{ ensure_directory_alias(other, original); }, "not a reference");
    rejects([&]{ ensure_directory_alias(root / "missing-alias", root / "absent"); }, "not an accessible directory");
    (void)write_text_if_changed(original / "marker.txt", "updated");
    require(text(alias / "marker.txt") == "updated", "original updates visible through alias");
    require(remove_file(alias) && text(original / "marker.txt") == "updated", "unlink preserves original");
    const auto relative_alias = root.filename() / "relative-reference";
    ensure_directory_alias(relative_alias, root.filename() / "alias-original");
    require(files_equivalent(relative_alias / "marker.txt", original / "marker.txt"), "relative directory alias");
    require(remove_file(relative_alias) && file_exists(original / "marker.txt"), "relative unlink preserves original");
#ifdef _WIN32
    rejects([&]{ ensure_directory_alias(root / "unc-reference", L"\\\\server\\share\\directory"); }, "require a local target");
    require(!file_exists(root / "unc-reference"), "unsupported target leaves no alias");
#endif
}
void check_build(const fs::path& root, const fs::path& repo, const fs::path& cmake,
                 const fs::path& fixture, const fs::path& aot, const fs::path& helper,
                 const fs::path& core, const fs::path& compiler, const fs::path& ninja,
                 const fs::path& config, unsigned target) {
    auto directory = root / "originals" / std::to_string(target) / fs::path(u8"a\u00e7\u00e3o \u65e5\u672c \U0001f680");
    fs::path name(u8"AOT_\u65e5\U0001f680.cpp");
    while ((directory / name).native().size() + 50 < target) directory /= "depth_012345678901234567890123456789";
    const auto length = (directory / name).native().size();
    if (length < target) name = fs::path(fs::path(std::string(target - length, 'x')).native() + name.native());
    const auto source = directory / name;
    const auto elf = directory / "synthetic.elf";
    const auto header = directory / "aot-dependency.hpp";
    io_test::run(fixture, {elf});
    io_test::run(aot, {elf, source});
    const std::string initial = text(source) + "\n#include \"aot-dependency.hpp\"\nint path_source_version() { return 1; }\nint path_header_version() { return PS2N_PATH_DEP; }\n";
    (void)write_text_if_changed(source, initial);
    (void)write_text_if_changed(header, "#define PS2N_PATH_DEP 1\n");
    const auto project = root / ("project-" + std::to_string(target));
    const auto build = root / ("build-" + std::to_string(target));
    const std::string script =
        "cmake_minimum_required(VERSION 3.21)\nproject(aot_path_probe LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "include(" + bracket(repo / "cmake/AotSource.cmake") + ")\n"
        "add_library(aot_object OBJECT)\nps2n_target_aot_source(aot_object \"${AOT_SOURCE}\")\n"
        "target_include_directories(aot_object PRIVATE " + bracket(repo / "include") + ")\n"
        "add_library(core STATIC IMPORTED)\nset_target_properties(core PROPERTIES IMPORTED_LOCATION " + bracket(core) + ")\n"
        "add_executable(path-probe " + bracket(repo / "tests/aot_smoke.cpp") + " $<TARGET_OBJECTS:aot_object>)\n"
        "target_include_directories(path-probe PRIVATE " + bracket(repo / "include") + ")\ntarget_link_libraries(path-probe PRIVATE core)\n"
        "file(GENERATE OUTPUT \"${CMAKE_BINARY_DIR}/objects.txt\" CONTENT \"$<JOIN:$<TARGET_OBJECTS:aot_object>,\\n>\")\n"
        "file(GENERATE OUTPUT \"${CMAKE_BINARY_DIR}/reference.txt\" CONTENT \"$<TARGET_PROPERTY:aot_object,PS2N_AOT_SOURCE_REFERENCE>\")\n";
    (void)write_text_if_changed(project / "CMakeLists.txt", script);
    io_test::run(cmake, {"-S", project, "-B", build, "-G", "Ninja", option("-DCMAKE_MAKE_PROGRAM:FILEPATH=", ninja),
        option("-DCMAKE_CXX_COMPILER:FILEPATH=", compiler), option("-DCMAKE_BUILD_TYPE:STRING=", config), option("-DAOT_SOURCE:FILEPATH=", source)}, 90);
    const auto reference = from_utf8(text(build / "reference.txt"));
    io_test::run(helper, {"equivalent", source, reference});
    const auto compile = [&]{ io_test::run(cmake, {"--build", build, "--target", "path-probe", "--parallel", "2"}, 90); };
    const auto executable = build / fs::path(
#ifdef _WIN32
        "path-probe.exe"
#else
        "path-probe"
#endif
    );
    compile();
    io_test::run(executable, {elf}); // 37 MIPS instructions, checked registers/RAM
    auto object_name = text(build / "objects.txt");
    while (!object_name.empty() && (object_name.back() == '\n' || object_name.back() == '\r')) object_name.pop_back();
    const auto object = from_utf8(object_name);
    const auto stamp = file_write_time(object);
    compile();
    require(file_write_time(object) == stamp, "unchanged build must not recompile");
    require(text(source) == initial, "build did not change original AOT");
    auto changed = initial;
    const auto site = changed.find("int path_source_version() { return 1; }");
    require(site != std::string::npos, "synthetic source mutation site");
    changed.replace(site, std::string("int path_source_version() { return 1; }").size(), "int path_source_version() { return 2; }");
    (void)write_text_if_changed(source, changed);
    compile();
    const auto source_stamp = file_write_time(object);
    require(source_stamp != stamp, "original source update must rebuild AOT object");
    (void)write_text_if_changed(header, "#define PS2N_PATH_DEP 2\n");
    compile();
    require(file_write_time(object) != source_stamp, "original header update must rebuild AOT object");
    io_test::run(executable, {elf});
    require(files_equivalent(source, reference), "source still has the same file identity");
#ifdef _WIN32
    const auto alias = reference.parent_path();
    require(remove_file(alias), "remove build-local junction");
    require(text(source) == changed && file_exists(header), "removing junction preserves original inputs");
#endif
    std::cout << "PASS Ninja AOT path length " << source.native().size()
              << ": native execution, same file identity, no-op build, original source/header rebuilds, safe alias cleanup\n";
}
} // namespace

template<class Char> int run_test(int argc, Char** argv) {
    fs::path cleanup;
    try {
        require(argc == 11, "root repo cmake fixture aot helper core compiler ninja config");
        if (!file_exists(argv[9])) { std::cout << "Ninja not available: AOT build path test NOT VALIDATED\n"; return 77; }
        const fs::path root(argv[1]);
        require(root.is_absolute() && !file_exists(root), "unused absolute test root");
        cleanup = root;
        check_alias(root);
        for (const auto target : {260u, 500u, 1000u})
            check_build(root, argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10], target);
        remove_directory_tree(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aot-build-paths: " << error.what() << '\n';
        if (!cleanup.empty()) { try { remove_directory_tree(cleanup); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; } }
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return run_test(argc, argv); }
#else
int main(int argc, char** argv) { return run_test(argc, argv); }
#endif
