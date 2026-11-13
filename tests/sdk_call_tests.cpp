#include "ps2_runtime_calls.h"

#include <iostream>

int main() {
    for (const auto name : {"sceIpuInit", "_sceIpuInit", "sceSifInitIopHeap", "_sceSifInitIopHeap"}) {
        if (!ps2_runtime_calls::resolveStubName(name).empty() || ps2_runtime_calls::isStubName(name)) {
            std::cerr << name << " must use the recompiled guest routine\n";
            return 1;
        }
    }
    if (ps2_runtime_calls::resolveStubName("_sceIpuSync") != "sceIpuSync" ||
        ps2_runtime_calls::resolveSyscallName("CreateThread") != "CreateThread") {
        std::cerr << "Unrelated runtime handlers must remain available\n";
        return 1;
    }
}
