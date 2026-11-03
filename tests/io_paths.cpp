// Filesystem + real CLI regression coverage, using only self-authored fixtures.
#include "ps2native/core.hpp"
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "io_process.hpp"
#include <iostream>
#include <string>

namespace {
using namespace ps2native;
using boot_checks::require;
using boot_checks::rejects;
namespace fs = std::filesystem;
fs::path option(const char* prefix, const fs::path& value) { auto p = fs::path(prefix); p += value.native(); return p; }
std::string text_file(const fs::path& path) { const auto b = read_file(path); return {b.begin(), b.end()}; }
void check_tree(const fs::path& root, const fs::path& cwd, const fs::path& aot,
                const fs::path& boot, const fs::path& fixture, const fs::path& cmake,
                const fs::path& cmake_script, std::size_t target) {
    fs::path dir = root / fs::path(u8"a\u00e7\u00e3o_\u65e5\u672c_\U0001f680 spaces & [] #!'");
    fs::path filename("bios.bin");
    while ((dir / filename).native().size() + 61 < target) dir /= "segment_012345678901234567890123456789012345";
    const auto current = (dir / filename).native().size();
    if (current < target) filename = fs::path(std::string(target - current, 'x') + "bios.bin");
    const auto leaf = [&](fs::path name) {
        const auto length = (dir / name).native().size();
        if (length < target) name = name.parent_path() / (fs::path(std::string(target - length, 'x')).native() + name.filename().native());
        return dir / name;
    };
    const auto bios_path = leaf("bios.bin");
    const auto elf_path = leaf("program.elf");
    const auto nvm_path = leaf(fs::path(u8"config_\u00e7_\u65e5\U0001f680.nvm"));
    const auto generated = leaf("generated.cpp");
    const auto trace_path = leaf(fs::path(u8"traces/\u65e5\U0001f680 trace.txt"));
    const auto bios_aot = leaf("bios_aot.cpp");
    require(bios_path.native().size() >= target, "requested path depth");
    ps2native::create_directories(dir);
    io_test::run(fixture, {bios_path, "bios"});
    auto bios_bytes = read_file(bios_path);
    const auto initial_bios = load_bios_rom0(bios_bytes);
    // Only the synthetic fixture's metadata is changed, to exercise CLI profile
    // and NVM validation with an existing profile; no real firmware is involved.
    const auto offset = initial_bios.find("ROMVER")->offset;
    const std::string romver = "0200AC20040614";
    std::copy(romver.begin(), romver.end(), bios_bytes.begin() + offset);
    require(write_binary_if_changed(bios_path, bios_bytes), "update synthetic ROMVER");
    const auto* profile = find_console_hardware("SCPH-70012:GH-032-22");
    require(profile != nullptr && profile->mechacon.has_value(), "NVM test profile");
    std::vector<std::uint8_t> nvm(kNvmBytes, 0);
    const auto& layout = nvm_layout_info(profile->mechacon->layout);
    std::copy(profile->model.begin(), profile->model.end(), nvm.begin() + layout.model_name);
    std::copy(profile->mechacon->model_id.begin(), profile->mechacon->model_id.end(), nvm.begin() + layout.ilink_id + 1);
    std::copy(profile->mechacon->con_model_id.begin(), profile->mechacon->con_model_id.end(), nvm.begin() + layout.console_id);
    require(write_binary_if_changed(nvm_path, nvm), "write synthetic NVM");
    require(load_nvm_image(read_file(nvm_path), *profile).model_name == profile->model, "NVM read and validation");
    require(load_bios_rom0(read_file(bios_path)).version.romver == romver, "BIOS read and validation");
    io_test::run(fixture, {elf_path, "m13"});
    require(parse_ee_elf(read_file(elf_path)).entry == 0x00100000u, "ELF read and validation");
    const auto relative_bios = bios_path.lexically_relative(cwd);
    const auto relative_nvm = nvm_path.lexically_relative(cwd);
    require(!relative_bios.empty() && !relative_bios.is_absolute(), "relative test path");
    require(read_file(relative_bios) == bios_bytes, "relative BIOS read");
    for (unsigned pass = 0; pass < 2; ++pass) {
        // The second invocation uses relative Unicode inputs and outputs.
        const auto input = pass ? elf_path.lexically_relative(cwd) : elf_path;
        const auto output = pass ? generated.lexically_relative(cwd) : generated;
        const auto timestamp = pass ? file_write_time(generated) : fs::file_time_type{};
        io_test::run(aot, {input, output});
        require(text_file(generated).find("run_recompiled") != std::string::npos, "AOT output complete");
        if (pass) require(file_write_time(generated) == timestamp, "AOT unchanged timestamp");
        const auto trace_time = pass ? file_write_time(trace_path) : fs::file_time_type{};
        io_test::run(boot, {pass ? relative_bios : bios_path, "--console=SCPH-70012:GH-032-22",
            option("--nvm=", pass ? relative_nvm : nvm_path), option("--trace-out=", pass ? trace_path.lexically_relative(cwd) : trace_path),
            "--max-instructions=64", "--trace=full"});
        const auto trace = text_file(trace_path);
        require(trace.find("# nvm sha256 " + sha256_hex(nvm)) != std::string::npos && trace.find("STOP ") != std::string::npos,
                "CLI NVM read, complete trace and flush");
        if (pass) require(file_write_time(trace_path) == trace_time, "identical trace timestamp");
    }
    io_test::run(aot, {relative_bios, bios_aot, "--bios"});
    const auto cmake_stamp = file_write_time(bios_aot);
    io_test::run(cmake, {option("-DPS2N_INPUT:FILEPATH=", relative_bios),
        option("-DPS2N_OUTPUT:FILEPATH=", bios_aot), option("-DPS2N_AOT:FILEPATH=", aot), "-P", cmake_script});
    require(file_write_time(bios_aot) == cmake_stamp, "CMake Unicode FILEPATH parameters and unchanged output");
#ifdef _WIN32
    const auto extended_bios = native_io_path(bios_path);
    const auto extended_nvm = native_io_path(nvm_path);
    require(native_io_path(extended_bios) == extended_bios, "prefix not duplicated");
    require(read_file(extended_bios) == bios_bytes, "explicit extended input");
    io_test::run(boot, {extended_bios, "--console=SCPH-70012:GH-032-22", option("--nvm=", extended_nvm),
        option("--trace-out=", native_io_path(trace_path)), "--max-instructions=64", "--trace=full"});
#endif
    const auto probe = dir / "probe.txt";
    require(write_text_if_changed(probe, "one"), "first text write");
    const auto stamp = file_write_time(probe);
    require(!write_text_if_changed(probe, "one") && file_write_time(probe) == stamp, "idempotent text write");
    require(write_text_if_changed(probe, "two") && text_file(probe) == "two", "same-size changed content");
    require(write_text_if_changed(probe, "") && read_file(probe).empty(), "empty output");
    require(!write_text_if_changed(probe, ""), "empty output unchanged");
    require(file_exists(probe) && remove_file(probe) && !file_exists(probe), "exists/remove normalized");
    std::cout << "PASS path length " << bios_path.native().size() << ": BIOS/NVM/ELF, AOT, traces, relative/absolute Unicode, idempotence\n";
}
} // namespace

template<class Char> int test_main(int argc, Char** argv) {
    fs::path cleanup;
    try {
        require(argc == 7 || argc == 8, "root aot boot fixture cmake script [UNC-root]");
        const fs::path root(argv[1]);
        require(root.is_absolute(), "absolute isolated test root");
        require(!file_exists(root), "test root must not already exist");
        cleanup = root;
        const auto cwd = root.parent_path();
#ifdef _WIN32
        require(native_io_path(L"\\\\server\\share\\folder\\file") == fs::path(L"\\\\?\\UNC\\server\\share\\folder\\file"), "UNC prefix mapping");
        const fs::path extended_unc(L"\\\\?\\UNC\\server\\share\\folder\\file");
        require(native_io_path(extended_unc) == extended_unc, "extended UNC preserved");
        const fs::path literal(L"\\\\?\\C:\\literal\\..\\file");
        require(native_io_path(literal) == literal, "literal extended syntax preserved");
        auto slashes = root.generic_wstring();
        require(native_io_path(slashes) == native_io_path(root), "forward slash normalization");
        require(native_io_path(root / "not-created" / ".." / "." / "probe") == native_io_path(root / "probe"), "ordinary dot components normalized");
        rejects([&]{ (void)native_io_path(std::wstring(32767, L'x')); }, "extended-length limit");
#endif
        for (auto target : {260u, 500u, 1000u}) check_tree(root / std::to_string(target), cwd, argv[2], argv[3], argv[4], argv[5], argv[6], target);
        rejects([&]{ (void)read_file(root / "missing.bin"); }, "Cannot open input file");
        rejects([&]{ (void)native_io_path(fs::path{}); }, "Invalid filesystem path");
        rejects([&]{ (void)native_io_path(fs::path(std::string("bad\0name", 8))); }, "Invalid filesystem path");
        rejects([&]{ (void)write_text_if_changed(root / std::string(300, 'x'), "invalid component"); }, "Cannot");
        const auto obstruction = root / "file-not-directory";
        (void)write_text_if_changed(obstruction, "file");
        rejects([&]{ ps2native::create_directories(obstruction / "child"); }, "Cannot create directories");
        rejects([&]{ (void)write_text_if_changed(root, "directory"); }, "not a regular file");
        // Optional real network I/O, never silently reported as covered.
        if (argc == 8 && fs::path(argv[7]) != fs::path("")) {
            const auto network_root = fs::path(argv[7]) / "ps2n-io-network-test";
            require(!file_exists(network_root), "network test root is unused");
            ps2native::create_directories(network_root);
            const auto network_file = network_root / fs::path(u8"\u65e5\U0001f680.txt");
            require(write_text_if_changed(network_file, "UNC") && text_file(network_file) == "UNC", "UNC real I/O");
#ifdef _WIN32
            require(text_file(native_io_path(network_file)) == "UNC", "extended UNC real I/O");
#endif
            remove_directory_tree(network_root);
            std::cout << "PASS real UNC I/O\n";
        } else std::cout << "UNC real network I/O NOT VALIDATED (no test share supplied)\n";
        remove_directory_tree(root);
        require(!file_exists(root), "long directory cleanup");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "io-paths: " << error.what() << '\n';
        if (!cleanup.empty()) { try { remove_directory_tree(cleanup); } catch (const std::exception& cleanup_error) { std::cerr << cleanup_error.what() << '\n'; } }
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return test_main(argc, argv); }
#else
int main(int argc, char** argv) { return test_main(argc, argv); }
#endif
