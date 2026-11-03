#include "ps2native/framebuffer.hpp"
#include "ps2native/io.hpp"
#include <stdexcept>

namespace ps2native {
void save_framebuffer_bmp(const std::filesystem::path& path, const Framebuffer& f) {
    const auto size = std::uint64_t{f.width} * f.height * 4u;
    if (!f.width || !f.height || f.width > 4096u || f.height > 4096u ||
        f.stride != f.width * 4u || f.rgba.size() != size)
        throw std::runtime_error("invalid RGBA8 framebuffer dimensions or storage");
    std::vector<std::uint8_t> bmp(static_cast<std::size_t>(size) + 54u);
    const auto put = [&](std::size_t at, std::uint32_t v, unsigned n = 4u) {
        for (unsigned b = 0; b < n; ++b) bmp[at + b] = static_cast<std::uint8_t>(v >> (b * 8u));
    };
    bmp[0] = 'B'; bmp[1] = 'M';
    put(2, static_cast<std::uint32_t>(bmp.size())); put(10, 54u); put(14, 40u);
    put(18, f.width); put(22, f.height); put(26, 1u, 2u); put(28, 32u, 2u);
    put(34, static_cast<std::uint32_t>(size));
    for (std::uint32_t y = 0; y < f.height; ++y)
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const auto src = (static_cast<std::size_t>(y) * f.width + x) * 4u;
            const auto dst = 54u + (static_cast<std::size_t>(f.height - 1u - y) * f.width + x) * 4u;
            bmp[dst] = f.rgba[src + 2u]; bmp[dst + 1u] = f.rgba[src + 1u];
            bmp[dst + 2u] = f.rgba[src]; bmp[dst + 3u] = f.rgba[src + 3u];
        }
    (void)write_binary_if_changed(path, bmp);
}
} // namespace ps2native
