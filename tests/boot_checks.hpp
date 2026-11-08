// Assertion helpers shared by the boot tests (M2.0-M2.3).
#pragma once

#include "ps2native/core.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace boot_checks {
inline void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error("assertion failed: " + reason);
}
template <typename F>
void rejects(F&& action, const std::string& diagnostic) {
    try { action(); }
    catch (const std::exception& error) {
        if (std::string(error.what()).find(diagnostic) != std::string::npos) return;
        throw std::runtime_error("Wrong rejection reason for '" + diagnostic + "': " + error.what());
    }
    throw std::runtime_error("Expected explicit rejection: " + diagnostic);
}
template <typename F>
void bus_fault(F&& action, ps2native::BusFault fault, const std::string& diagnostic) {
    try { action(); }
    catch (const ps2native::BusError& error) {
        require(error.fault == fault, std::string("bus fault kind for ") + diagnostic + ": " + error.what());
        require(std::string(error.what()).find(diagnostic) != std::string::npos,
                "bus fault text '" + diagnostic + "': " + error.what());
        return;
    }
    throw std::runtime_error("Expected bus fault: " + diagnostic);
}
// A modeled device refused the access (BusFault::Device with this text).
template <typename F>
void device_refused(F&& action, const std::string& diagnostic) {
    bus_fault(std::forward<F>(action), ps2native::BusFault::Device, diagnostic);
}
inline std::string hex8(std::uint32_t value) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (int shift = 28; shift >= 0; shift -= 4) out.push_back(kDigits[(value >> shift) & 15u]);
    return out;
}
inline bool contains(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
}
// Sequence numbers and engine markers restart at each invocation.
inline std::vector<std::string> normalized(const std::vector<std::string>& trace) {
    std::vector<std::string> lines;
    for (const auto& line : trace) {
        if (line.starts_with("E ") || line.starts_with("STOP")) continue;
        if (line.starts_with("I ")) lines.push_back("I * *" + line.substr(16));
        else lines.push_back(line);
    }
    return lines;
}
inline ps2native::BootReport run_native_sliced(ps2native::EEState& state,ps2native::Memory& memory,
        const ps2native::AotModule& module,ps2native::BootOptions options,std::uint64_t slice) {
    using namespace ps2native;
    const auto budget=options.max_instructions;
    std::uint64_t retired=0,aot=0;
    std::vector<std::string> trace,tail;
    BootReport result;
    do {
        options.max_instructions=std::min(slice,budget-retired);
        result=run_ee_boot(state,memory,&module,options);
        retired+=result.retired;aot+=result.aot_instructions;
        require(result.interpreted_instructions==0,"native slicing cannot interpret");
        trace.insert(trace.end(),result.trace.begin(),result.trace.end());
        tail.insert(tail.end(),result.tail.begin(),result.tail.end());
        if(tail.size()>options.tail_instructions)tail.erase(tail.begin(),tail.end()-options.tail_instructions);
        if(result.outcome!=BootOutcome::BudgetExhausted || !result.retired)break;
    } while(retired<budget);
    result.retired=retired;result.aot_instructions=aot;
    result.trace=std::move(trace);result.tail=std::move(tail);
    return result;
}
inline void same_trace(const std::vector<std::string>& left,const std::vector<std::string>& right) {
    const auto a=normalized(left),b=normalized(right);
    const auto mismatch=std::mismatch(a.begin(),a.end(),b.begin(),b.end());
    require(mismatch.first==a.end() && mismatch.second==b.end(),
        "native trace mismatch at "+std::to_string(mismatch.first-a.begin())+
        " ("+std::to_string(a.size())+"/"+std::to_string(b.size())+"):\n"+
        (mismatch.first==a.end()?"<end>":*mismatch.first)+"\n"+
        (mismatch.second==b.end()?"<end>":*mismatch.second));
}
} // namespace boot_checks
