#pragma once
#include "ps2native/disc.hpp"
#include "ps2native/console.hpp"
#include <deque>
#include <map>
#include <memory>
#include <vector>
#include <chrono>
namespace ps2native::engine {
class Console;
class IopCdvd {
public:
    explicit IopCdvd(Console& c):console(c),rtc_epoch(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()){}
    Console& console;
    enum class Medium{Auto,Cd,Dvd};
    Medium medium{Medium::Auto};
    std::unique_ptr<DiscImage> media;
    const ConsoleHardware* hardware{};
    std::map<std::uint32_t,std::uint32_t> dma{{0x1f8010b0,0},{0x1f8010b4,0},{0x1f8010b8,0},{0x1f8010f0,0},{0x1f8010f4,0},{0x1f801570,0},{0x1f801574,0}};
    std::vector<std::uint8_t> nparams,sparams;
    std::deque<std::uint8_t> sresults;
    std::uint8_t ncommand{},scommand{},irq{},error{},status{},howto{0x80};
    std::uint64_t due{~0ull},rtc_epoch{},sector_ticks{};
    std::uint32_t position{},remaining{},read_lba{},active_irq{2};
    std::uint32_t read(std::uint32_t address,unsigned bytes);
    void write(std::uint32_t address,std::uint32_t value,unsigned bytes);
    void complete();
    void scmd(std::uint8_t command);
    void mount(const std::filesystem::path& path,Medium kind=Medium::Auto);
    bool irq_ready() const;
    std::uint32_t irq_line() const;
    void update_dicr();
};
}
