// Synthetic ISO 9660 images for launcher tests: a primary volume descriptor,
// a root directory and an optional SYSTEM.CNF. Independently authored; the
// serials used by tests are placeholders, not titles.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace iso_test {
struct IsoOptions {
    std::string volume_id{"PS2N_TEST"};
    std::string system_cnf{"BOOT2 = cdrom0:\\SLUS_000.01;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"};
    unsigned raw_mode{};          // 0: 2048-byte sectors; 1 or 2: raw 2352-byte Mode 1 / Mode 2 Form 1
    std::uint32_t sectors{24};    // volume space size (PVD)
    std::uint32_t written{24};    // sectors in the returned image (< sectors: truncated image)
    std::uint32_t cnf_lba{19};    // SYSTEM.CNF extent; beyond the image: the caller writes it
};

inline void put32(std::uint8_t* p, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put32_both(std::uint8_t* p, std::uint32_t v) {
    put32(p, v);
    for (unsigned i = 0; i < 4; ++i) p[4 + i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
}
inline std::size_t directory_record(std::uint8_t* p, std::uint32_t lba, std::uint32_t size, bool directory,
                                    const std::string& name) {
    const std::size_t length = (33 + name.size() + 1) & ~std::size_t{1};
    std::memset(p, 0, length);
    p[0] = static_cast<std::uint8_t>(length);
    put32_both(p + 2, lba);
    put32_both(p + 10, size);
    p[25] = directory ? 0x02u : 0x00u;
    p[32] = static_cast<std::uint8_t>(name.size());
    std::memcpy(p + 33, name.data(), name.size());
    return length;
}

inline std::vector<std::uint8_t> build_iso(const IsoOptions& options) {
    constexpr std::size_t kSector = 2048;
    constexpr std::uint32_t kLayout = 24; // descriptors, root directory and files
    std::vector<std::uint8_t> user(static_cast<std::size_t>(kLayout) * kSector, 0);
    auto sector = [&](std::uint32_t lba) { return user.data() + lba * kSector; };
    std::uint8_t* pvd = sector(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::memset(pvd + 8, ' ', 64);
    std::memcpy(pvd + 40, options.volume_id.data(), std::min<std::size_t>(32, options.volume_id.size()));
    put32_both(pvd + 80, options.sectors);
    pvd[128] = 0x00; pvd[129] = 0x08; pvd[130] = 0x08; pvd[131] = 0x00; // 2048, both byte orders
    directory_record(pvd + 156, 18, kSector, true, std::string(1, '\0'));
    std::memcpy(pvd + 813, "2026110300000000", 16);
    std::uint8_t* terminator = sector(17);
    terminator[0] = 255;
    std::memcpy(terminator + 1, "CD001", 5);
    terminator[6] = 1;
    std::uint8_t* root = sector(18);
    std::size_t at = directory_record(root, 18, kSector, true, std::string(1, '\0'));
    at += directory_record(root + at, 18, kSector, true, std::string(1, '\1'));
    at += directory_record(root + at, 21, 4, false, "README.TXT;1");
    if (!options.system_cnf.empty()) {
        directory_record(root + at, options.cnf_lba, static_cast<std::uint32_t>(options.system_cnf.size()), false, "SYSTEM.CNF;1");
        if (options.cnf_lba < kLayout) std::memcpy(sector(options.cnf_lba), options.system_cnf.data(), std::min(kSector, options.system_cnf.size()));
    }
    std::memcpy(sector(21), "test", 4);

    user.resize(static_cast<std::size_t>(options.written) * kSector, 0);
    if (options.raw_mode == 0) return user;
    const std::size_t offset = options.raw_mode == 1 ? 16 : 24;
    std::vector<std::uint8_t> raw(user.size() / kSector * 2352, 0);
    for (std::size_t s = 0; s < user.size() / kSector; ++s) {
        std::uint8_t* out = raw.data() + s * 2352;
        out[0] = 0;
        std::fill(out + 1, out + 11, std::uint8_t{0xFF});
        out[11] = 0;
        out[15] = static_cast<std::uint8_t>(options.raw_mode);
        std::memcpy(out + offset, user.data() + s * kSector, kSector);
    }
    return raw;
}
} // namespace iso_test
