# ps2autotests EE ALU subset

Source: [unknownbrackets/ps2autotests](https://github.com/unknownbrackets/ps2autotests/tree/97469ffbed8631277b94e28d01dabd702aa97ef3),
revision `97469ffbed8631277b94e28d01dabd702aa97ef3`.
`alu.expected` and `LICENSE.md` are unchanged upstream files.
The expected file SHA-256 is
`ca8e2f3e76df48f98b2ebc89b4d1181dd5b407e76e6bae642cfec5a933223c76`.

`../ps2autotests_alu.hpp` adapts the twelve `C_*` operand pairs from
`tests/cpu/ee/alu.cpp` for each of `addu`, `and`, `daddu`, `dsubu`, `nor`,
`or`, `slt`, `sltu`, `subu`, and `xor`. The constant vectors come from
`tests/cpu/ee/shared.h`. Copyright and ISC permission are preserved here
and in that adapter. No expected arithmetic is calculated by the adapter.

CMake generates a bare-metal ELF with LQ, the tested instruction, and SQ.
The existing offline generator compiles it to AOT. CTest
`hardware-autotests-ee-alu` runs both shared execution paths and compares
all four words of each of 120 results with the independent `.expected`
rows. BREAK is an explicit static boundary; the runner stops before it.
The native run must retire every instruction through AOT.

This is an instruction-level adaptation, not execution of the original
ps2sdk-linked test ELF. The original harness requires ps2sdk, ps2link,
printf transport, and kernel startup/exit services. Those services are not
provided by Direct ELF and are not replaced with successful stubs. Other
upstream ALU rows and EE/IOP/VU/DMA/GS programs are outside this subset;
this test does not report them as passed or certify physical hardware.
