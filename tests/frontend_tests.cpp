// Launcher core (no GUI): content probes, the persistent library and settings,
// runtime capability contracts and ps2n-boot argument/output handling.
// Synthetic content only: a generated ROM0 container and generated ISO images.
#include "iso_image.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/frontend/library.hpp"
#include "ps2native/frontend/runtime.hpp"
#include "ps2native/io.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#endif

namespace fs = std::filesystem;
using namespace ps2native;
using namespace ps2native::frontend;

namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}
template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const std::string& what) {
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": got '" << actual << "', expected '" << expected << "'\n";
    }
}
void write(const fs::path& path, const std::vector<std::uint8_t>& bytes) { write_file_atomically(path, bytes); }

// A directory several hundred UTF-16 units deep with non-ASCII components.
fs::path deep_unicode_root(const fs::path& base) {
    fs::path root = base / fs::path(u8"biblioteca-ção-日本語");
    while (root.native().size() < 300) root /= fs::path(u8"pasta-çãõ-長いパス-0123456789");
    return root;
}

void test_disc_probes(const fs::path& dir) {
    const auto iso = dir / fs::path(u8"Jogo de Teste (ção).iso");
    write(iso, iso_test::build_iso({}));
    auto r = probe_disc(iso);
    check_eq(probe_status_name(r.status), std::string("valid"), "ISO status " + r.error);
    check_eq(r.info["serial"], std::string("SLUS-00001"), "ISO serial");
    check_eq(r.info["region"], std::string("NTSC-U"), "ISO region");
    check_eq(r.info["platform"], std::string("PlayStation 2"), "ISO platform");
    check_eq(r.info["video_mode"], std::string("NTSC"), "ISO video mode");
    check_eq(r.info["disc_version"], std::string("1.00"), "ISO version");
    check_eq(r.info["volume_id"], std::string("PS2N_TEST"), "ISO volume id");
    check_eq(r.info["volume_date"], std::string("2026-11-03"), "ISO volume date");
    check(!r.info.contains("warning"), "complete ISO has no truncation warning");
    check(!r.info.contains("cover"), "no cover without an image");

    // A user cover next to the image is referenced, not copied.
    auto cover = iso;
    cover.replace_extension(".png");
    write(cover, {0x89, 'P', 'N', 'G'});
    r = probe_disc(iso);
    check(path_from_utf8(r.info["cover"]).filename() == cover.filename(), "sibling cover");
    // A covers folder named by serial wins.
    const auto covers = dir / "covers";
    write(covers / "SLUS-00001.jpg", {0xFF, 0xD8});
    r = probe_disc(iso, ProbeContext{covers});
    check(r.info["cover"].ends_with("SLUS-00001.jpg"), "covers folder by serial");

    for (const unsigned mode : {1u, 2u}) {
        iso_test::IsoOptions options;
        options.raw_mode = mode;
        const auto raw = dir / ("raw-mode" + std::to_string(mode) + ".bin");
        write(raw, iso_test::build_iso(options));
        r = probe_disc(raw);
        check_eq(probe_status_name(r.status), std::string("valid"), "raw status " + r.error);
        check(r.info["format"].find("2352") != std::string::npos, "raw format named");
        check_eq(r.info["media"], std::string("CD"), "raw images are CDs");
        check_eq(r.info["serial"], std::string("SLUS-00001"), "raw serial");
    }

    iso_test::IsoOptions truncated;
    truncated.sectors = 400;
    const auto cut = dir / "truncated.iso";
    write(cut, iso_test::build_iso(truncated));
    r = probe_disc(cut);
    check(r.status == ProbeStatus::Valid && r.info.contains("warning"), "truncated image warns");

    iso_test::IsoOptions no_cnf;
    no_cnf.system_cnf.clear();
    write(dir / "data.iso", iso_test::build_iso(no_cnf));
    r = probe_disc(dir / "data.iso");
    check(r.status == ProbeStatus::Invalid && r.error.find("SYSTEM.CNF") != std::string::npos, "ISO without SYSTEM.CNF refused");

    iso_test::IsoOptions ps1;
    ps1.system_cnf = "BOOT = cdrom:\\SCES_000.02;1\r\n";
    write(dir / "ps1.iso", iso_test::build_iso(ps1));
    r = probe_disc(dir / "ps1.iso");
    check_eq(r.info["platform"], std::string("PlayStation"), "PS1 platform");
    check_eq(r.info["region"], std::string("PAL"), "PS1 region");
    LibraryEntry ps1_entry;
    ps1_entry.kind = ContentKind::Disc;
    apply_probe(ps1_entry, r, {});
    check(compatibility_of(ps1_entry) == Compatibility::NotPlayStation2, "PS1 disc is not a PS2 title");
    check(!needs_attention(compatibility_of(ps1_entry)), "an identified PS1 disc needs no attention");

    std::vector<std::uint8_t> cso(4096, 0);
    std::memcpy(cso.data(), "CISO", 4);
    write(dir / "game.cso", cso);
    r = probe_disc(dir / "game.cso");
    check(r.status == ProbeStatus::Unsupported, "CSO is unsupported, explicitly");

    write(dir / "noise.iso", std::vector<std::uint8_t>(64 * 1024, 0x5A));
    r = probe_disc(dir / "noise.iso");
    check(r.status == ProbeStatus::Invalid, "non-ISO refused");
    r = probe_disc(dir / "absent.iso");
    check(r.status == ProbeStatus::Missing, "absent image is missing");

    check_eq(normalize_serial("SLUS_209.46"), std::string("SLUS-20946"), "serial normalization");
    check_eq(normalize_serial("SCES-50490"), std::string("SCES-50490"), "serial with dash");
    check_eq(normalize_serial("MAIN.ELF"), std::string(), "not a serial");
    check_eq(region_from_serial("SLPM-12345"), std::string("NTSC-J"), "NTSC-J prefix");
}

// SYSTEM.CNF stored past 4 GiB: offsets are 64-bit and the image is read by range.
void test_large_image(const fs::path& dir) {
    constexpr std::uint32_t kLba = 2'200'000; // 4.19 GiB
    iso_test::IsoOptions options;
    options.sectors = kLba + 1;
    options.cnf_lba = kLba;
    const auto big = dir / fs::path(u8"grande-ção.iso");
    write(big, iso_test::build_iso(options));
    const auto native = native_io_path(big);
#ifdef _WIN32
    const HANDLE handle = CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "open large image");
    DWORD returned = 0;
    const bool sparse = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    if (!sparse) {
        std::cout << "note: filesystem without sparse files; large-image test skipped\n";
        remove_file(big);
        return;
    }
#endif
    {
        std::fstream file(native, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(static_cast<std::streamoff>(kLba) * 2048);
        const std::string cnf = "BOOT2 = cdrom0:\\SLES_123.45;1\r\nVMODE = PAL\r\n";
        std::string sector(2048, '\0');
        sector.replace(0, cnf.size(), cnf);
        file.write(sector.data(), static_cast<std::streamsize>(sector.size()));
        check(static_cast<bool>(file), "write past 4 GiB");
    }
    check(regular_file_size(big) > 0x100000000ull, "image larger than 4 GiB");
    auto r = probe_disc(big);
    check_eq(r.info["serial"], std::string("SLES-12345"), "serial read past 4 GiB");
    check_eq(r.info["video_mode"], std::string("PAL"), "video mode past 4 GiB");
    check(!r.info.contains("warning"), "large image complete");
    remove_file(big);
}

void test_bios_and_nvm(const fs::path& dir, const fs::path& rom0) {
    auto r = probe_bios(rom0);
    check_eq(probe_status_name(r.status), std::string("valid"), "synthetic ROM0 " + r.error);
    check_eq(r.info["sha256"].size(), std::size_t{64}, "ROM0 sha256");
    check(!r.info["romver"].empty() && !r.info["region"].empty(), "ROM0 ROMVER parsed by the core");
    check(!r.details.empty() && r.details.front().starts_with("RESET"), "ROMDIR listed, RESET first");
    check(r.info.contains("warning") == r.info["profiles"].empty(), "profile warning only without profiles");

    write(dir / "short.bin", std::vector<std::uint8_t>(4096, 0));
    r = probe_bios(dir / "short.bin");
    check(r.status == ProbeStatus::Invalid && !r.error.empty(), "short file refused by the core parser");
    check(probe_bios(dir / "none.bin").status == ProbeStatus::Missing, "absent ROM0 missing");

    // Profiles come from the core database.
    const auto profiles = console_profiles_for_romver("0200EC20040614");
    check_eq(profiles.size(), std::size_t{2}, "SCPH-70004 boards for ROMVER 0200EC20040614");

    write(dir / "console.nvm", std::vector<std::uint8_t>(1024, 0));
    r = probe_nvm(dir / "console.nvm", "");
    check(r.status == ProbeStatus::Pending, "NVM needs a profile");
    r = probe_nvm(dir / "console.nvm", "SCPH-00000:XX");
    check(r.status == ProbeStatus::Invalid, "unknown profile refused");
    r = probe_nvm(dir / "console.nvm", "SCPH-77008");
    check(r.status == ProbeStatus::Invalid && !r.error.empty(), "blank NVM refused by load_nvm_image");
    write(dir / "wrong.nvm", std::vector<std::uint8_t>(100, 0));
    r = probe_nvm(dir / "wrong.nvm", "SCPH-77008");
    check(r.status == ProbeStatus::Invalid, "NVM size refused");
}

void test_library(const fs::path& dir) {
    const auto root = deep_unicode_root(dir);
    const auto disc = root / fs::path(u8"Título — ç.iso");
    write(disc, iso_test::build_iso({}));
    check(native_io_path(disc).native().size() > 300, "deep path exceeds MAX_PATH");

    Library library;
    auto [entry, inserted] = library.add(ContentKind::Disc, disc, 1700000000);
    check(inserted, "first add inserts");
    const auto id = entry->id;
    check_eq(entry->title, path_utf8(fs::path(u8"Título — ç")), "default title is the stem");
    apply_probe(*entry, probe_disc(disc), stamp_file(disc));
    entry->favorite = true;
    entry->error = "line one\nline two \\ end";
    check(compatibility_of(*entry) == Compatibility::RuntimeUnavailable, "identified PS2 disc: runtime unavailable");
    check(!needs_probe(*entry, stamp_file(disc)), "unchanged file needs no probe");
    check(library.add(ContentKind::Disc, disc, 1).second == false, "same file not duplicated");

    auto [bios, bios_inserted] = library.add(ContentKind::Bios, dir / "short.bin", 1700000001);
    check(bios_inserted, "bios added");
    bios->console_profile = "SCPH-77008";
    bios->nvm_path = dir / "console.nvm";
    apply_nvm_probe(*bios, probe_nvm(bios->nvm_path, bios->console_profile));

    const auto file = root / fs::path(u8"dados") / "library.ps2nlib";
    library.save(file);
    const auto restored = Library::load(file);
    check_eq(restored.entries().size(), std::size_t{2}, "restored entries");
    check(restored.serialize() == library.serialize(), "round trip is lossless");
    const auto* again = restored.find(id);
    check(again != nullptr && again->path == library.find(id)->path && again->path == normalize_library_path(disc), "Unicode deep path restored");
    check(again != nullptr && again->error == "line one\nline two \\ end", "escaped values restored");
    check(again != nullptr && again->favorite, "favorite restored");
    const auto* restored_bios = restored.find(bios->id);
    check(restored_bios != nullptr && restored_bios->nvm_status == ProbeStatus::Invalid &&
          restored_bios->console_profile == "SCPH-77008", "BIOS configuration restored");

    // Saving again leaves no temporary file behind.
    library.save(file);
    auto temporary = file;
    temporary += ".tmp";
    check(!file_exists(temporary), "atomic save cleans up");

    // Change detection.
    write(disc, iso_test::build_iso({.volume_id = "CHANGED", .sectors = 30, .written = 30}));
    check(needs_probe(*entry, stamp_file(disc)), "changed file needs a probe");
    remove_file(disc);
    check(!stamp_file(disc).exists, "removed file stamp");
    check(needs_probe(*entry, stamp_file(disc)), "missing file needs a probe");

    check(library.remove(id) && library.find(id) == nullptr, "remove");
    check(Library::parse("").entries().empty(), "empty text is an empty library");
    check(Library::load(root / "absent.ps2nlib").entries().empty(), "absent file is an empty library");
    bool refused = false;
    try { (void)Library::parse("ps2-native library 2\n"); } catch (const std::runtime_error&) { refused = true; }
    check(refused, "newer major version refused");
    refused = false;
    try { (void)Library::parse("something else\n"); } catch (const std::runtime_error&) { refused = true; }
    check(refused, "foreign file refused");
    const auto tolerant = Library::parse("ps2-native library 1\n[entry]\nid=a\npath=/x.iso\nfuture_key=1\n[end]\n");
    check_eq(tolerant.entries().size(), std::size_t{1}, "unknown keys ignored");

    LauncherSettings settings;
    settings.game_folders = {root, dir};
    settings.covers_folder = root / "covers";
    settings.active_bios = "0123456789abcdef";
    settings.diagnostic_budget = 25'000'000;
    settings.diagnostic_trace = "full";
    settings.renderer = "native";
    settings.reduce_motion = true;
    settings.scan_folders_on_start = false;
    settings.library_view = "list";
    const auto settings_file = root / "settings.cfg";
    settings.save(settings_file);
    const auto loaded = LauncherSettings::load(settings_file);
    check(loaded.serialize() == settings.serialize(), "settings round trip");
    check(loaded.game_folders.size() == 2 && loaded.game_folders[0] == root, "settings Unicode folder");
    check(LauncherSettings::parse("ps2-native settings 1\nrenderer=bogus\n").renderer == "gs-faithful", "invalid setting ignored");
}

// Multi-track CD dump: the sheet is one disc, identified from its data track.
void test_cue_sheets(const fs::path& dir) {
    const auto folder = dir / fs::path(u8"cue-ção");
    ps2native::create_directories(folder);
    iso_test::IsoOptions options;
    options.raw_mode = 2;
    options.system_cnf = "BOOT = cdrom:\\SLUS_000.07;1\r\n";
    write(folder / fs::path(u8"Jogo (Track 01).bin"), iso_test::build_iso(options));
    write(folder / fs::path(u8"Jogo (Track 02).bin"), std::vector<std::uint8_t>(2352 * 8, 0x11));
    const std::string sheet = "\xEF\xBB\xBF" "FILE \"Jogo (Track 01).bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
                              "FILE \"Jogo (Track 02).bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n";
    const auto cue = folder / fs::path(u8"Jogo ção.cue");
    write(cue, std::vector<std::uint8_t>(sheet.begin(), sheet.end()));

    const auto parsed = parse_cue_sheet(cue);
    check_eq(parsed.tracks.size(), std::size_t{2}, "CUE tracks parsed");
    check(parsed.data_track() != nullptr && parsed.data_track()->number == 1, "CUE data track is track 1");
    auto r = probe_disc(cue);
    check_eq(probe_status_name(r.status), std::string("valid"), "CUE probe status " + r.error);
    check_eq(r.info["serial"], std::string("SLUS-00007"), "CUE serial from the data track");
    check_eq(r.info["cue_tracks"], std::string("2"), "CUE track count");
    check(r.info["format"].starts_with("CUE sheet"), "CUE format named");

    const auto listed = find_candidates(folder, ContentKind::Disc);
    check_eq(listed.size(), std::size_t{1}, "folder scan lists the sheet, not its tracks");
    check(!listed.empty() && listed.front().filename() == cue.filename(), "the sheet is the listed disc");

    const std::string audio_only = "FILE \"Jogo (Track 02).bin\" BINARY\n  TRACK 01 AUDIO\n";
    write(folder / "audio.cue", std::vector<std::uint8_t>(audio_only.begin(), audio_only.end()));
    check(probe_disc(folder / "audio.cue").status == ProbeStatus::Invalid, "audio-only sheet refused");
    const std::string missing = "FILE \"absent.bin\" BINARY\n  TRACK 01 MODE2/2352\n";
    write(folder / "missing.cue", std::vector<std::uint8_t>(missing.begin(), missing.end()));
    check(probe_disc(folder / "missing.cue").status == ProbeStatus::Missing, "sheet with a missing data track");
    write(folder / "empty.cue", std::vector<std::uint8_t>{'R', 'E', 'M', '\n'});
    check(probe_disc(folder / "empty.cue").status == ProbeStatus::Invalid, "sheet without tracks refused");
    remove_directory_tree(folder);
}

void test_candidates(const fs::path& dir) {
    const auto disc_files = find_candidates(dir, ContentKind::Disc);
    check(std::any_of(disc_files.begin(), disc_files.end(),
                      [](const fs::path& p) { return p.filename() == fs::path(u8"Jogo de Teste (ção).iso"); }),
          "folder scan finds disc images");
    for (const auto& p : disc_files) check(!path_utf8(p).starts_with("\\\\?\\"), "scan results are display paths");
}

void test_runtime_contracts() {
    for (const auto& c : runtime_capabilities()) {
        check(!c.reason.empty(), "capability reason");
        if (c.capability != Capability::BootDiagnostic) check(!c.available, std::string(c.name) + " unavailable today");
    }
    check(capability_status(Capability::BootDiagnostic).available, "boot diagnostic available");

    DiagnosticRequest request;
    request.rom0 = fs::path(u8"C:/bios/ção.bin");
    request.console_profile = "SCPH-77008:GH-051";
    request.nvm = fs::path("console.nvm");
    request.expected_sha256 = std::string(64, 'a');
    request.max_instructions = 1234;
    const auto args = diagnostic_arguments(request);
    check_eq(args.size(), std::size_t{6}, "diagnostic argument count");
    check_eq(args[0], path_utf8(request.rom0), "ROM0 first");
    check_eq(args[2], std::string("--console=SCPH-77008:GH-051"), "console flag");
    check_eq(args[4], std::string("--max-instructions=1234"), "budget flag");
    bool refused = false;
    request.console_profile.clear();
    try { (void)diagnostic_arguments(request); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "NVM without console refused");

    DiagnosticSummary summary;
    for (const char* line : {"# romver 0200EC20040614: version 2.00, region Europe (E), CEX, built 2004-06-14",
                             "# engine interpreter only", "# | Initialize Start.\r",
                             "# IOP: 2210789 instructions, PC 0xBFC4A8B0, stopped",
                             "# result block point; retired 17690079 (aot 0, interpreter 17690079, engine switches 0)",
                             "# block point 0xBFC4A8B0 [bus: device not implemented] EE bus R32 (LW) at 0xB000F000"})
        parse_diagnostic_line(summary, line);
    check(summary.finished, "result seen");
    check_eq(summary.outcome, std::string("block point"), "outcome");
    check_eq(summary.retired, std::uint64_t{17690079}, "retired");
    check_eq(summary.block_pc, std::string("0xBFC4A8B0"), "block pc");
    check_eq(summary.block_category, std::string("bus: device not implemented"), "block category");
    check_eq(summary.block_reason, std::string("EE bus R32 (LW) at 0xB000F000"), "block reason");
    check(summary.sio_lines.size() == 1 && summary.sio_lines[0] == "Initialize Start.", "SIO output");
    check_eq(summary.engine, std::string("interpreter only"), "engine");
    DiagnosticSummary failed;
    parse_diagnostic_line(failed, "ps2n-boot: ROM0 SHA-256 mismatch");
    check(!failed.finished && failed.tool_error == "ROM0 SHA-256 mismatch", "tool error");
}
} // namespace

template <typename Character>
int frontend_main(int argc, Character** argv) {
    // Fixture for the launcher UI test: a synthetic PS2 disc image.
    if (argc == 3 && fs::path(argv[1]) == fs::path("--write-iso")) {
        try {
            write(fs::path(argv[2]), iso_test::build_iso({.volume_id = "PS2N_SYNTHETIC"}));
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 3) {
        std::cerr << "Usage: ps2n-frontend-tests <synthetic-rom0.bin> <work-dir>\n";
        return 2;
    }
    try {
        const fs::path rom0(argv[1]);
        const fs::path dir = fs::path(argv[2]) / fs::path(u8"frontend-ção");
        remove_directory_tree(dir);
        ps2native::create_directories(dir);
        test_disc_probes(dir);
        test_large_image(dir);
        test_bios_and_nvm(dir, rom0);
        test_library(dir);
        test_cue_sheets(dir);
        test_candidates(dir);
        test_runtime_contracts();
        remove_directory_tree(dir);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " launcher core check(s) failed\n";
        return 1;
    }
    std::cout << "launcher core: all checks passed\n";
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return frontend_main(argc, argv); }
#else
int main(int argc, char** argv) { return frontend_main(argc, argv); }
#endif
