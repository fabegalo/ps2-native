#include "ps2recomp/Emitters/function_table_emitter.h"
#include <iostream>
#include <stdexcept>

int main() {
    using Emitter = ps2recomp::FunctionTableEmitter;
    try {
        const auto ranges = Emitter::compact({{0x1000, "a"}, {0x1004, "a"}, {0x100c, "a"},
                                             {0x1010, "b"}, {0x1014, "b"}, {0x1024, "c"}}, 0x1000);
        std::vector<std::string> table(10);
        for (const auto& range : ranges)
            for (uint32_t i = 0; i < range.count; ++i) table.at(range.first + i) = range.name;
        if (table != std::vector<std::string>{"a", "a", "", "a", "b", "b", "", "", "", "c"})
            throw std::runtime_error("Compaction changed bindings or filled null holes");
        if (!Emitter::compact({}, 0).empty()) throw std::runtime_error("Empty table acquired bindings");
        std::vector<std::pair<uint32_t, std::string>> dense;
        for (uint32_t i = 0; i < 200000; ++i) dense.emplace_back(0x100000 + i * 4, "owner");
        const auto compact = Emitter::compact(dense, 0x100000);
        if (compact.size() != 1 || compact[0].first != 0 || compact[0].count != dense.size())
            throw std::runtime_error("Dense resume points did not compact without loss");
        std::cout << "PASS EE table bindings, null holes, distinct owners, empty and large tables\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
