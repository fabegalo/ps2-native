# Referências técnicas versus requisitos do ps2-native — 03/11/2026

> Observações baseadas no README e em arquivos específicos dos repositórios consultados. Não são prova de que todo código desses projetos funciona em jogos reais. **Nenhuma dependência de código foi integrada.**

| Subsistema / necessidade | PS2Recomp (GPL-3.0) | PCSX2 (GPL-3.0+) | Diretriz ps2-native |
|---|---|---|---|
| Leitura ELF e análise de função | `ps2xRecomp/src/lib/elf_parser.cpp`, `control_flow_analyzer.cpp`, análise ELF via módulos próprios; suporte a análise/anotações manuais e Ghidra | Leitura ELF, programa de inicialização e runtime maduro | M0 próprio, sem dependências; M1 tratar indiretos, símbolos, relocações e overlays |
| EE / MIPS R5900 | Emissão C++ e decodificador R5900, tradutores de MMI/VU e exceções incompletas | Interpretador e recompiladores dinâmicos do EE; base de testes comportamentais | AOT nativo por fluxo seguro; preservar ciclos/semântica, fallback explícito sem sucesso falso |
| IOP / IRX / RPC | Módulo `ps2xIOP`, com HLE e extensibilidade; alvo da pesquisa, sem garantia de cobertura | Serviços e hardware IOP consolidados ao longo do tempo | Criar IOP completo com serviços e sincronização reais, não apenas mocks |
| VU0/VU1 microprogramas | Tradutores e suporte VU0 macro; `vu_translator.cpp` contém casos ainda comentados como não implementados | Arquitetura VU, VIF, DMA e sincronização | Micro e macro VU obrigatórios antes de compatibilidade real |
| GS / vídeo / áudio | Runtime oferece caminhos básicos de GS/VU/arquivo/sistema descritos como stubs | GS, SPU2 e integração com timing mais completos | Modo GS fiel primeiro, Native aprimorado depois; não reutilizar código GPL |
| BIOS / browser original | Não comprova suporte genérico à navegação da BIOS original | Exige BIOS original do usuário e oferece boot/navegação como referência | Experiência original é prioridade e precisa de host dedicado, provavelmente híbrido |
| Multiplataforma | README indica testes principalmente MSVC e alguns caminhos SSE/AVX | Windows, Linux e macOS (consultar requisitos por arquitetura) | Abstrair SIMD e host; CI nos seis alvos, validação real por plataforma |
| Launcher e banco de jogos | Ferramenta de desenvolvedor, não produto genérico de consumidor | Interface e gerenciamento de jogos de emulador | Launcher próprio apenas como frontend do sistema real |

## Outros casos com execução nativa declarada

- **[OpenGOAL](https://github.com/open-goal/jak-project)**: portabilidade real de *Jak and Daxter* via decompilação GOAL, recompilador próprio e runtime. Repositório descreve Jak 1 como polido, Jak 2 beta e Jak 3 em desenvolvimento. **Não fornece AOT genérico para qualquer ELF R5900.**
- **[GT4Recomp](https://github.com/moesuito/gt4-staticrecomp)**: repositório reivindica recompilação estática de EE e microprogramas VU para *Gran Turismo 4*, com hardware PS2 modelado em software, sistema gráfico híbrido. Os números de FPS e compatibilidade publicados são **relatos do próprio projeto, não testes independentes nossos**. É específico de uma versão do jogo, não um runtime universal.

**Conclusão:** usar como referências de solução por subsistema e testes de equivalência. Não reutilizar código GPL num repositório puramente BSD. A dificuldade essencial não é gerar C++ para MIPS: é reproduzir precisamente temporização, comportamento de memória, VU, DMA, IOP, GS, BIOS e recursos dinâmicos do PS2.

Fontes:
- https://github.com/ran-j/PS2Recomp/blob/main/README.md
- https://github.com/ran-j/PS2Recomp/blob/main/ps2xRecomp/src/lib/control_flow_analyzer.cpp
- https://github.com/ran-j/PS2Recomp/blob/main/ps2xRecomp/src/lib/vu_translator.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Interpreter.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Memory.cpp
- https://github.com/open-goal/jak-project
- https://github.com/moesuito/gt4-staticrecomp

## Consulta para a M1.3 — 03/11/2026

Somente leitura de comportamento; **nenhum trecho foi copiado ou traduzido**.

- **PCSX2** `pcsx2/R5900OpcodeImpl.cpp` e `pcsx2/Interpreter.cpp`: confirmam comparações de 64 bits em `BEQ/BNE/BLEZ/BGTZ/BLTZ/BGEZ/SLT/SLTI/SLTIU`, `ORI/ANDI/XORI` em 64 bits com imediato estendido com zero, deslocamentos de 32 bits com resultado estendido com sinal, `MULT/MULTU` de três operandos (LO também em `rd`), resultados de `DIV/DIVU` para divisão por zero e `INT32_MIN / -1`, link sempre escrito em `BxxZAL(L)`, anulação do delay slot em branch-likely não tomado, `LQ/SQ` ignorando os 4 bits baixos e `SYNC` sem efeito no interpretador. O ps2-native reimplementou essas regras de forma independente e as verifica com fixture sintético.
- **PS2Recomp** `ps2xRecomp/src/lib/elf_parser.cpp`, `ps2_recompiler.cpp`: relocations são carregadas, jump tables vêm de configuração (`m_config.jumpTables`) e há varredura heurística de `LUI/ADDIU`. O ps2-native **não** adota configuração por jogo nem aceita endereços por semelhança: exige relocation verificada ou prova no código (`SLTIU` + `BEQ $zero` + tabela escalada).
- **ABI MIPS SysV / binutils:** pareamento `R_MIPS_HI16` com o próximo `R_MIPS_LO16` do mesmo símbolo, vários `HI16` por `LO16`, `%hi` com ajuste de carry para o `%lo` com sinal, e `R_MIPS_26` dentro da região de 256 MiB de `PC + 4`.

Fontes adicionais:
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp
- https://github.com/ran-j/PS2Recomp/blob/main/ps2xRecomp/src/lib/elf_parser.cpp
- https://github.com/ran-j/PS2Recomp/blob/main/ps2xRecomp/src/lib/ps2_recompiler.cpp
## Consulta para a M2.0 — 03/11/2026

Somente leitura de comportamento e formato; **nenhum trecho foi copiado ou traduzido**.

- **PCSX2** `pcsx2/R5900.cpp` (`cpuReset`): PC `0xBFC00000`, `PRid = 0x2E20`, `Config = 0x440`; o PCSX2 também liga CU0–CU2 em `Status`, o ps2-native usa só `BEV | ERL` (os demais campos do reset não são garantidos e o firmware escreve `Status` antes de depender deles). `pcsx2/COP0.cpp`: layout de `EntryHi/EntryLo/PageMask`, bit S do scratchpad, G efetivo só com as duas metades, `TLBR/TLBP`, `ERET` via `ErrorEPC`/`EPC`, condição de efetividade de `EI/DI`, máscara de bits de `Status`. `pcsx2/Cache.cpp`: códigos das operações `CACHE`. `pcsx2/Hw.h` e `pcsx2/Memory.cpp`: mapa físico do EE (timers, IPU, GIF, VIF, FIFOs, DMAC, INTC, SIO, SBUS, MCH, VU, GS, IOP, ROM0/1/2). `pcsx2/ps2/BiosTools.cpp`: localização do ROMDIR pela entrada `RESET`, arquivos em sequência alinhados a 16 bytes, campos de `ROMVER` e códigos de região.
- **ps2sdk** `tools/romimg/src/romimg.h`/`romimg.c`: formato da entrada ROMDIR (nome de 10 bytes, tamanho de EXTINFO, tamanho do arquivo), ordem obrigatória `RESET`/`ROMDIR`/`EXTINFO`, tamanho de `ROMDIR` incluindo o terminador, campos EXTINFO (`valor`, `tamanho`, `tipo`: data, versão, comentário, `0x7F`) com dados alinhados a 4 bytes.
- **SHA-256:** FIPS 180-4 e seus vetores de teste oficiais.
- Diferenças deliberadas em relação ao PCSX2: TLB indefinida até ser escrita (o PCSX2 zera), kuseg com `ERL = 1` recusado, `Count`/`Random` não lidos sem temporização, exceções e interrupções param em vez de vetorizar — todas registradas como limitações, nunca como sucesso.

Fontes adicionais:
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/COP0.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Cache.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Hw.h
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/ps2/BiosTools.cpp
- https://github.com/ps2dev/ps2sdk/tree/master/tools/romimg/src

## Consulta para a M2.1 — 03/11/2026

Somente leitura de comportamento; **nenhum trecho foi copiado ou traduzido**. O firmware do usuário foi executado e desmontado apenas localmente; nenhum byte, trace ou tradução dele entrou no repositório.

- **ps2tek** (documentação de hardware): layout de `Tn_COUNT/MODE/COMP/HOLD`, fontes de relógio (BUSCLK, /16, /256, HBLANK), linha NTSC de 9370 ciclos de BUSCLK, `Count` incrementado a cada ciclo do EE, EE a 294,912 MHz.
- **PCSX2** `pcsx2/MMI.cpp`: `MULT1/MULTU1/DIV1/DIVU1/MFHI1/MFLO1/MTHI1/MTLO1` com as regras do pipeline 0 nas metades altas de LO/HI e `PADDUW` com saturação por palavra. `pcsx2/HwWrite.cpp`: escritas sem caso próprio, como `0x1000F500`, vão para armazenamento simples ("Mystery Regs"). `pcsx2/ps2/Iop/IopHwRead.cpp`: leitura de 8 bits em `0x1F803204` devolve `0x7C`, comentada como *"TOOL board id"* — registrada como referência, **não adotada** sem evidência do console de varejo. O histórico do arquivo mostra que `0x7C` entrou no commit `c1d46b1866` (*"IOP: TOOL config registers"*). Antes dele, o endereço era lido do armazenamento genérico, e `0x1F803100` devolvia `0xFF`, comentado como estado padrão do console de varejo. **PS2Ident** (`ps2homebrew/PS2Ident`: `sysman/main.c`, `ps2db.h`, `PS2Ident.db`, `README.md`), consultado só como dados: `BoardInf` vem do *boot mode* 6 do IOP; `MPUBoardID` é a leitura direta de `0xBF803800`; o banco associa `0x10..0x7C` a placas de ferramenta (Gmain, B3system, MPU-4.0) e registra `BoardInf = 0` e `MPUBoardID = 0xFFFF` nos 144 consoles de varejo e DEX catalogados. **ps2sdk** `common/include/iop_regs.h`: `A_IOP_BF803204` de 8 bits, sem descrição. Nenhum código foi copiado. psx-place e psdevwiki recusaram o acesso (HTTP 403). `pcsx2/Hw.h`: a região `0x1000F400` (MCH) é descrita como pouco conhecida.
- **BIOS real do usuário** (comportamento observado): grava a entrada 0 da TLB com PageMask 0 e bit S e usa a pilha em `0x70003FF0`, o que fundamenta a cobertura de 16 KiB da entrada S e a hipótese de que entradas não escritas não correspondem; converte sua medição de HBLANK com 15734 Hz (taxa de linha NTSC).
- Diferença em relação à M2.0: a TLB deixou de recusar traduções enquanto houver entradas indefinidas (o PCSX2 as zera; o ps2-native as considera não correspondentes e continua recusando `TLBR` delas).

Fontes adicionais:
- https://psi-rockin.github.io/ps2tek/
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/HwWrite.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/ps2/Iop/IopHwRead.cpp
