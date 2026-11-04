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

## Consulta para a M2.2 — 03/11/2026

Somente leitura de comportamento e de layouts; **nenhum trecho foi copiado ou traduzido**. O firmware do usuário foi executado e desmontado apenas localmente; nenhum byte, trace ou tradução dele entrou no repositório.

- **ps2sdk** (AFL-2.0, consultado só como documentação):
  - `common/include/gs_privileged.h`: endereços e campos de `SMODE1`, `SMODE2`, `SRFSH`, `SYNCH1/2`, `SYNCV` e `CSR`; os layouts definem os bits reservados recusados.
  - `ee/graph/src/graph_mode.c`: sequência `SMODE1` com `PRST|SINT` e depois sem ambos para iniciar o modo de TV; CMOD 2 = NTSC, 3 = PAL.
  - `ee/kernel/include/sio.h` e `ee/kernel/src/sio.c`: nomes e endereços do EE SIO, `putc` com espera no bit 15 de `SIO_ISR`.
  - `common/include/dev9regs.h`: janela DEV9 `0x1F801460..0x1F80147F`, onde `0x1470/0x1472` aparecem só por endereço.
- **ps2tek:** comprimento de linha NTSC (9370 BUSCLK) e PAL (9436 BUSCLK).
- **PCSX2** (GPL-3.0+, só comportamento observável):
  - `pcsx2/GS.cpp`: só `CSR` e `SIGLBLID` são legíveis, e `CSR.RESET` limpa os registradores privilegiados.
  - `pcsx2/Counters.cpp`: sem eventos de HBLANK/VBLANK enquanto `SMODE1.SINT = 1`.
  - `pcsx2/HwRead.cpp`/`HwWrite.cpp`:
    - `SIO_ISR` lê 0 sem dados recebidos;
    - `0x1000F410` é um registrador desconhecido que lê 0 ("Mystery Regs");
    - `MCH_RICM`/`MCH_DRD` usam valores fixos de `INIT`/`CNFGA`/`CNFGB`, que **não foram adotados** por não serem documentação.
  - `pcsx2/DEV9/DEV9.cpp`: `0x1470/0x1472` são tratados como escritas desconhecidas.
- **BIOS real do usuário** (comportamento observado):
  - valores escritos nos registradores do GS;
  - calibração do relógio por HBLANK;
  - pulso DEV9;
  - texto pelo SIO;
  - uso de `LWC1`/`SWC1`;
  - par `LUI`/`ORI`/`JR` no vetor de reset;
  - uso do bloco MCH `0x1000F400–0x1000F4C0` (comando `F410` com bit de ocupado).

Fontes adicionais:
- https://github.com/ps2dev/ps2sdk/blob/master/common/include/gs_privileged.h
- https://github.com/ps2dev/ps2sdk/blob/master/ee/graph/src/graph_mode.c
- https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/sio.h
- https://github.com/ps2dev/ps2sdk/blob/master/common/include/dev9regs.h
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Counters.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/HwRead.cpp

## Consulta para a M2.3 — 03/11/2026

Somente leitura de especificação e de comportamento; **nenhum trecho foi copiado ou traduzido**. Os datasheets foram lidos localmente e não entraram no repositório. O firmware do usuário foi executado e desmontado apenas localmente.

- **Rambus, *Direct RDRAM 64/72-Mbit Advance Information* (3/12/98):**
  - formatos `SRQ`/`SA`/`SINT`/`SD` e comprimentos das transações;
  - códigos `SOP`;
  - registrador `INIT` (0x021) e valores de reset;
  - Algorithm InitDev;
  - `tCYCLE1` mínimo de 1000 ns.
- **Samsung K4R271669F** (128 Mbit, F-die, v1.4 2003): `tCYCLE1` de 1000 ns nas transações seriais. Este datasheet não traz o mapa de registradores.
- **PCSX2** (GPL-3.0+, só comportamento observável):
  - `pcsx2/HwRead.cpp`/`HwWrite.cpp`: `MCH_RICM`/`MCH_DRD` com respostas fixas de `INIT`/`CNFGA`/`CNFGB`, e `F410` desconhecido.
  - **Nada adotado:** as respostas fixas não são documentação.
- **MAME** `ps2sony.cpp` (BSD-3-Clause, consultado só como comportamento): mesma abordagem de respostas fixas e nenhum mapeamento de `F410`.
- **ps2tek** (fork israpps), página RDRAM_Init: repete a lógica dos emuladores e não cobre `F410`.
- **BIOS real do usuário** (comportamento observado):
  - layout de `MCH_RICM` nas rotinas seriais;
  - espera do bit 31;
  - enumeração por `INIT`;
  - ordem dos acessos ao bloco `F400–F4C0`;
  - leitura de `0x1000F520`;
  - uso de `SOP 0xB`/`0xE`.
- **Não obtido:** datasheet de 128/144 Mbit com o mapa de registradores e os `SOP` estendidos. Os espelhos recusaram download automatizado.

Fontes adicionais:
- https://retrocdn.net/images/6/68/Direct_RDRAM_datasheet.pdf
- https://www.digchip.com/datasheets/download_datasheet.php?id=467210&part-number=K4R271669F-TbCS8
- https://github.com/mamedev/mame/blob/master/src/mame/sony/ps2sony.cpp

## Consulta para a M2.4 — 03/11/2026

Somente leitura de especificação e de comportamento; **nenhum trecho foi copiado ou traduzido**.

- **Sony, *EE User's Manual* 6.0** (documentação do PS2 Linux Kit, espelho em ninjadynamics/PS2Docs; lida localmente, não incluída):
  - mapa de endereços do EE, onde o bloco `0x1000F400–F4F0` não aparece;
  - `D_ENABLER`/`D_ENABLEW`: só `CPND` (bit 16) é definido, e `D_ENABLER` lê o conteúdo de `D_ENABLEW`.
- **Rambus 64/72-Mbit** (já registrado na M2.3): layouts de `TPARM`, `TFRM`, `TRDLY`, `TCYCLE` e `TEST75–79`.
- **ps2tek:** `D_ENABLER` com bits "lixo, mas graváveis", e a SCPH-39001 "parece esperar" `0x1201`. Esse valor **não foi adotado**.
- **Iris** (allkern/iris, MIT, só comportamento): mesmas respostas fixas do PCSX2 para `MCH_DRD`, `F410` lendo 0 e `D_ENABLER = 0x1201`. **Nada adotado.**
- **PCSX2** `pcsx2/Hw.cpp` (GPL-3.0+, só comportamento): `D_ENABLER`/`D_ENABLEW = 0x1201` no reset. **Não adotado:** sem justificativa de hardware para os modelos testados.
- **BIOS real do usuário** (comportamento observado):
  - tabela de 16 gerações do MCH indexada por `D_ENABLER`;
  - escrita de `F480`/`F4C0` conforme a geração;
  - valores escritos em `TEST77`/`TCYCLE`.

Fontes adicionais:
- https://github.com/ninjadynamics/PS2Docs
- https://psi-rockin.github.io/ps2tek/
- https://github.com/allkern/iris
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Hw.cpp

## Consulta para a M2.7 — 03/11/2026

Somente comportamento e formatos; **nenhum trecho foi copiado ou traduzido**. Nenhum firmware, NVM ou trace entrou no repositório.

- **ps2tek:** tempos verticais NTSC/PAL (262/312 linhas, 22/26 de *blank*), BUSCLK = EE/2, fontes de IRQ do IOP (0 VBLANK início, 3 DMA, 11 VBLANK fim), `DICR`/`DICR2` (flag mestre), canais DMA do IOP e *tags* de cadeia, registradores do SIF (`MSCOM`/`SMCOM`/`MSFLG`/`SMFLG`, `CTRL`, `BD6`), "GIF I/O" (`GIF_CTRL`, `GIF_MODE`, `GIF_STAT`) e o modo de cadeia do DMAC do EE.
- **ps2sdk** (AFL-2.0, só documentação):
  - `common/include/gs_privileged.h`: layout dos campos do `SYNCV` e do `SMODE1`.
  - `iop/system/sifman/src/sifman.c` ("baseado no SCE SDK 3.1.0"): condição de residência por `BD6`, espera de `MSFLG 0x10000`, `sceSifSetDChain` e o formato das *tags* do canal 9 (endereço/IRQ/fim, palavras, *tag* do EE com `QWC` arredondado).
- **PCSX2** (GPL-3.0+, só comportamento observável): o IOP lê 0 em `0x1D000060`; os flags do SIF começam em 0. Os dois viraram **hipóteses** nomeadas, não valores copiados.
- **Busca na web** pelo bloco `0x1F801900`: nenhuma documentação pública encontrada.

Fontes adicionais:
- https://psi-rockin.github.io/ps2tek/
- https://github.com/ps2dev/ps2sdk/blob/master/iop/system/sifman/src/sifman.c
- https://github.com/ps2dev/ps2sdk/blob/master/common/include/gs_privileged.h
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/IopMem.cpp

## Consulta para a M2.6 — 03/11/2026

Somente comportamento, formatos e dados; **nenhum trecho foi copiado ou traduzido**. Nenhuma NVM, firmware ou trace entrou no repositório; a NVM sintética usada no diagnóstico foi gerada e mantida só localmente.

- **PS2Ident** (`ps2homebrew/PS2Ident`), consultado como dados e documentação:
  - `DumpMECHACON_EEPROM`: imagem de 1024 bytes com as palavras 0..511 de `sceCdReadNVM`.
  - `sysman/dev9.c`: "ssbus revision" é `DEV9_REG(DEV9_R_REV)`. `sysman/iLink.c`: o PHY do i.LINK é lido por `PHYAccess`; o registrador `0x7C` aparece só como "UnknownRegister7C" (desliga/religa o LINK).
  - `PS2Ident.db`: versão do MECHACON, ModelID, ConModelID, SSBUS revision e dados do PHY do i.LINK por modelo, placa e ROMVER.
- **ps2sdk** (AFL-2.0, só documentação):
  - `iop/cdvd/cdvdman`: parâmetros e respostas de `ReadNVM`, `OpenConfig`, `ReadConfig` e `CloseConfig`; checksum do bloco.
  - `iop/system/eeconf/src/eeconf.c` (reimplementação do `EECONF`): ordem dos comandos S, `eeconf_dve_magic` (`0x1F803204`, `DEV9_R_REV`, `0xB600000A`), `eeconf_ieee1394_available`/`_reset`.
  - `common/include/dev9regs.h`: `DEV9_R_REV` = `0xBF80146E`. `iop/dev9/dev9/src/ps2dev9.c`: nibble alto `0x20` = CXD9566 (PCMCIA), `0x30` = CXD9611 (*expansion bay*).
  - `iop/arcade/acata`: a janela `0xB6000000` como área de dispositivo da *expansion bay* (ATA nos arcades).
- **ps2tek:** timers do IOP (registradores, bits de modo, prescalers, larguras), fontes de `I_STAT` e regra de entrega (`I_CTRL && (I_STAT & I_MASK)`).
- **PCSX2** (GPL-3.0+, só comportamento observável):
  - `pcsx2/CDVD/CDVD.cpp`: layout da NVM "BIOS v1.70+" (áreas de configuração, i.LINK ID e console ID); o formato `.nvm` é o mesmo da imagem do PS2Ident.
  - `pcsx2/FW.cpp`: comentário de um desenvolvedor de que `0x1F80847C` lê `0x10000001` no próprio console de varejo; adotado como **hipótese** nomeada.
- **psdevwiki:** offset do nome do modelo (`0x1B0`) nas NVM Dragon. Novos acessos ao site foram bloqueados por verificação anti-robô, que não foi contornada.

Fontes adicionais:
- https://github.com/ps2homebrew/PS2Ident
- https://github.com/ps2dev/ps2sdk/blob/master/iop/system/eeconf/src/eeconf.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/dev9/dev9/src/ps2dev9.c
- https://psi-rockin.github.io/ps2tek/
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/FW.cpp

## Consulta para a M2.8 — 03/11/2026

Somente comportamento, formatos e dados; **nenhum trecho foi copiado ou traduzido**. Nenhuma BIOS, NVM, trace ou tradução entrou no repositório (a pasta local `bios/` está no `.gitignore`).

- **Firmware do usuário (dumps locais, lidos por desmontagem própria):** módulo `ROMGSCRT` e kernel do EE (cópia em RAM). As funções de leitura e escrita do codificador de vídeo (endereços `0x42`/`0x43`, ponteiro de registrador), os registradores `0x7E`/`0x7F`/`0xB1`, a condição `DEV9_R_REV & 0xF0 == 0x30`, as sequências dos dois mestres (`0x1A000000`, `0x1F801900`), o pacote PATH3 do `SetGsCrt` (44 escritas A+D, tabela em `0x800159C8`) e a contagem de 30 HBLANKs com `SMODE1.PRST = 1`. A semântica foi inferida do uso; o código do firmware não foi reproduzido.
- **PS2Ident.db** (dados): entradas da SCPH-70004 (ROMVER `0200EC20040614`, placas GH-032-11/GH-032-32, MECHACON 6.2/6.4, ModelID `27D420`, ConModelID `26D4`, EE 3.1 / GS 1.11).
- **ps2tek:** GIF (`GIF_CTRL`, `GIF_MODE`, `GIF_STAT`, FIFO), fontes do `INTC` do EE, DMA do IOP (`CHCR`, `BCR`, *tags* de cadeia, `DPCR`, `DICR`) e DMAC do EE (`CHCR`, *tags* de origem/destino, `D_CTRL`/`D_STAT`/`D_PCR`/`D_SQWC`).
- **ps2sdk** (AFL-2.0, só documentação): `gif_tags.h` (campos do *GIFtag*), `gs_gp.h` (endereços dos registradores gerais), `iop/system/sifman/src/sifman.c` (formato das entradas de transferência do SIF, `CHCR`/`BCR` dos canais 9/10, `sceSifSetDChain`).
- **PCSX2** (GPL-3.0+, só comportamento observável): `pcsx2/Memory.cpp` (o bloco em `0x1A000000` chamado "DVE"), `pcsx2/GS.cpp` (reset por `CSR`), `pcsx2/Sif.h`/`Sif0.cpp`/`Sif1.cpp` (enquadramento palavra/*quadword* do SIF e preenchimento de pacotes curtos, descritos como resultados de testes no console). Nada foi adotado como fato: o que serviria ao DMA do SIF fica registrado como referência até haver medição ou teste próprio.

Fontes adicionais:
- https://github.com/ps2homebrew/PS2Ident
- https://psi-rockin.github.io/ps2tek/
- https://github.com/ps2dev/ps2sdk/blob/master/common/include/gif_tags.h
- https://github.com/ps2dev/ps2sdk/blob/master/iop/system/sifman/src/sifman.c
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Memory.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS.cpp
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Sif.h

## Consulta para compilacao AOT em caminhos profundos

Somente APIs, formatos e comportamento; nenhum codigo do Ninja foi incorporado.
A implementacao do projeto permanece independente, na fronteira de host I/O.

- [Ninja 1.13.1, disk_interface.cc](https://github.com/ninja-build/ninja/blob/v1.13.1/src/disk_interface.cc): consulta do limite legado no Stat e dependencia do estado de long paths do processo. Usado apenas para explicar o bloqueio observado.
- [Microsoft: REPARSE_DATA_BUFFER](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_reparse_data_buffer): tag e layout do buffer de mount point/junction; offsets e comprimentos em bytes.
- [Microsoft: Hard Links and Junctions](https://learn.microsoft.com/en-us/windows/win32/fileio/hard-links-and-junctions): referencia a diretorios locais, inclusive entre volumes, e restricoes de rede.
- [CMake 3.21: try_compile](https://cmake.org/cmake/help/v3.21/command/try_compile.html): bootstrap de executavel a partir dos fontes originais e instalacao do helper proprio no build.
- MSVC 19.44 localizado e Ninja 1.13.1 locais: prefixo /showIncludes emitido em UTF-8 e decodificado como OEM no registro do CMake; a captura dos bytes originais restabeleceu o rebuild por header alterado, comprovado pelo teste sintetico.

## Fast track M3.0: soluções genéricas e limites de reutilização

| Implementação estudada | Organização observada | Aplicação neste núcleo |
|---|---|---|
| [gow-native-pc, runtime fixado](https://github.com/Vnnc1/gow-native-pc/blob/d28a63d3fd0f2a412ff9018b52d2a9a3cbdb18fd/engine/runtime.toml) | Pipeline externo PS2Recomp/Tobiichi, geradores EE/VU1/IOP, captura e recompilação offline | Aproveitar a separação conceitual entre identificação, geração e runtime; manter código dinâmico verificável e medir engine switches. Nenhum código GPL integrado |
| [gow1, perfil](https://github.com/Vnnc1/gow-native-pc/blob/d28a63d3fd0f2a412ff9018b52d2a9a3cbdb18fd/games/gow1/game.toml) | Mapa de funções, overrides de GoW, endereços de frame, lista manual de heads VU1 e scripts de gameplay | Elementos específicos de título/revisão; excluídos do pipeline genérico. Nem progresso relatado pelo upstream nem meta de FPS provam resultado neste projeto |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/README.md) | Analisador ELF/símbolos/relocations, R5900→C++, registro de funções e syscalls; também admite stubs, patches e overrides por endereço | Metadados/CFG e fronteiras explícitas são princípios úteis; stubs de sucesso, function maps por título e requisitos SSE/AVX não foram adotados |
| [PS2Recomp IOP](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xIOP/README.md) | IRX executado em R3000A, kernel virtual/imports e serviços HLE; RAM separada, transporte SIF | A separação de RAM e bytes transferidos é genérica. Aqui executa-se apenas o payload próprio e DMA modelado; IRX/HLE não foram simulados por retornos inventados |
| [Play!](https://github.com/jpd002/Play-/blob/1f27232772e484bfb79364b751b84f41f4c87481/README.md) | Emulador com BIOS HLE; interfaces EE DMAC/INTC/SIF, VIF→VU/GIF, executor VU e backend GS | Hardware com licença permissiva é candidato futuro. As interfaces consultadas dependem de CMIPS, callbacks, Framework e save states; os serviços de IopBios são acoplados ao kernel HLE. Importá-los integralmente duplicaria o núcleo ou substituiria a política de boot |
| [PCSX2](https://github.com/PCSX2/pcsx2) | Referência comportamental de R5900/IOP, exceções, SIF/DMA, VIF/VU e GS; análise inicial nos relatórios anteriores | Comparar comportamentos documentados, sem copiar, adaptar, traduzir ou vincular GPL. Nenhum resultado deste incremento é apresentado como comparação executada com PCSX2 |

Avaliação específica de Play!: `Source/ee/{Dmac_Channel.h,DMAC.h,INTC.cpp,SIF.h,Vif1.h,VuExecutor.h}`,
`Source/gs/GSHandler.h` e `Source/iop/{Iop_DmacChannel.h,IopBios.cpp}`. O design de interfaces e
as tabelas de instruções VU foram consultados; não houve transplante. Decisão de integração e
licenças em [LICENSE-DECISION](LICENSE-DECISION.md). Mantém-se um único C++20 por dispositivo.

Evidência para implementações independentes:

- [EE Core User's Manual v6](https://docs.alexrp.com/mips/ee.pdf), Count/Compare, pp. 70/72,
  Status pp. 73–74 e Interrupt Exception p. 99: igualdade, IP7, reconhecimento por Compare,
  máscaras, EXL, BEV, EPC/BD e vetor. Nenhum documento foi incorporado ao repositório.
- [EE User's Manual](https://manuals.plus/m/55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365),
  mapa de canais e VIF DIRECT; [ps2tek](https://psi-rockin.github.io/ps2tek/#sifdma), IOP DMA,
  tags, DPCR2, DICR/DICR2, DMACEN/DMACINTEN e espera por FIFO. A latência do barramento e a
  capacidade/throughput escolhidos continuam hipóteses de modelo explicitadas no relatório.

O frame próprio verifica VIF1 DIRECT→GIF PATH2→GS. Não verifica VU ou serviços de jogo.
Resultados reproduzíveis e limites de aceitação: [M3.0](M3.0-STATUS.md).

## Developer toolchain

Consulta ao PS2Recomp em `e5636981a1c9de368f6b71e819c3dc188dfedbf4`:
`ps2xAnalyzer/src/{analyzer_main,analysis_passes}.cpp`,
`ps2xRecomp/tools/ghidra/ExportPS2Functions.java` e `ps2xStudio/src/main.cpp`.
O Analyzer organiza descoberta/configuração; o exportador oferece CSV
`Name,Start,End,Size` com fim exclusivo; Studio encaminha análise pelo
estado da aplicação. Reutilizadas apenas ideias de fluxo/formato. Não
foram incorporados código GPL, heurísticas de funções, bancos comerciais,
stubs ou patches. O projeto mantém seu próprio CFG conservador e núcleo.

Identificadores de contêiner IRX/seção de módulo conferidos no
[ps2sdk](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/tools/ps2-irxgen/src/elftypes.h)
e `irxtypes.h` da mesma revisão. Não houve integração de código AFL/GPL.
Referências completas e evidência local em [DEV-TOOLS](DEV-TOOLS.md).
