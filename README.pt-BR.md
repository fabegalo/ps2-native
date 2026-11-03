# ps2-native — Recompilação nativa do PlayStation 2
**Idiomas:** [English](README.md) | Português (Brasil)


> **Status:** protótipo experimental, M2.2 em andamento. Ainda não é um emulador PS2 nem um launcher; não executa o Browser nem jogos comerciais. Dumps reais de BIOS executam as primeiras ~637.600 instruções do EE e param num registrador não modelado. Nada é renderizado.

## Novidades da M2.2 — registradores privilegiados do GS e a configuração de memória da BIOS

Os mesmos três dumps agora retiram **637.620** instruções (SCPH-70012/77008) e **637.598** (SCPH-90006), de forma idêntica no interpretador puro e no modo híbrido. Eles:

- programam o CRTC do GS;
- calibram o relógio do EE contando bordas de HBLANK com T0 e `Count`;
- pulsam o DEV9;
- imprimem `# Initialize memory (rev:3.70, ctm:393Mhz, cpuclk:295Mhz detected)` pelo EE SIO (`rev:4.00` na SCPH-90006).

Param em `0x9FC4135C`, no `SW` em `0x1000F410`, registrador do controlador de memória (MCH) sem documentação pública. A recusa explícita permanece até haver evidência.

- **Registradores privilegiados do GS** (só estado de controle, sem renderização):
  - `SMODE1/SMODE2/SRFSH/SYNCH1/SYNCH2/SYNCV` aceitam escritas de 64 bits; bits reservados são recusados e leituras também (são só de escrita).
  - `CSR` aceita escritas de `RESET` e dos bits de reconhecimento. A leitura do `CSR` é recusada porque o valor depende do VBLANK e da revisão do GS.
- **Relógio de linha do GS gera o HBLANK:**
  - `SMODE1.SINT` suspende o sincronismo; linhas NTSC (9370 BUSCLK) ou PAL (9436) começam quando ele é liberado.
  - Retemporizar o GS com um timer contando HBLANK é recusado.
- **Latches DEV9, transmissão do EE SIO, `LWC1`/`SWC1`:**
  - `LWC1`/`SWC1` movem palavras cruas; sem `Status.CU1` param como coprocessador inutilizável.
  - O resto do SIO e da FPU é recusado.
- **Prova AOT de saltos constantes `LUI`/`ORI`/`JR`:**
  - Um salto só é provado quando não há entrada entre o `LUI` e o `JR` e o `LUI` não está em delay slot.
  - A cobertura estática da BIOS sobe de 12 para 107 instruções (2 saltos provados). O código que a BIOS copia para a RAM continua interpretado.
- **17/17 testes** aprovados localmente (MSVC, LLVM clang++); a CI usa somente firmware sintético. [Evidências, hipóteses e bloqueio da M2.2](docs/M2.2-STATUS.md).

## M2.1 — primeiras etapas de uma BIOS real

Três dumps de ROM0 fornecidos pelo usuário (SCPH-70012 v2.00 EUA, SCPH-77008 v2.20 Europa, SCPH-90006 v2.20 Japão; nunca guardados no repositório) iniciam no vetor de reset, passam pela configuração de COP0/TLB, pela escrita em `0x1000F500`, pela pilha no scratchpad e por `PADDUW`, e entram no módulo `RDRAM` da ROM, retirando **70.090 instruções** de forma idêntica no interpretador puro e no modo híbrido. Leem o byte *tool board id* em `0x1F803204`, seguem o caminho de varejo e param na primeira escrita em registrador privilegiado do GS (`SMODE1`, `0x12000010`). O GS não é modelado.

- **Tool board id `0x1F803204`:** lê `0xFF`, ou seja, sem placa de ferramenta. Esse valor é uma hipótese registrada.
  - O firmware só distingue classes de placa de ferramenta (nibble alto 1–3 no EE, `0x60/0x61` no IOP).
  - `0xFF` (barramento alto) é sustentado por dados do PS2Ident, mas o byte exato no varejo não foi medido.
  - Outros tamanhos, escritas e o resto da janela de registradores do IOP são recusados.

- **MMI do pipeline 1 e `PADDUW`** na semântica compartilhada AOT/interpretador (`MULT1/MULTU1/DIV1/DIVU1/MFHI1/MFLO1/MTHI1/MTLO1` sobre LO1/HI1).
- **Relógio do EE e `COP0 Count`:** um ciclo por instrução retirada (hipótese registrada); timers T0–T3 do EE (`COUNT/MODE/COMP`, BUSCLK, /16, /256 e HBLANK NTSC) derivam do mesmo relógio. Gate, interrupções de timer, flags de modo e `HOLD` são recusados com motivo.
- **TLB corrigida pela BIOS real:** entrada com bit S mapeia os 16 KiB do scratchpad com qualquer PageMask; entradas nunca escritas não correspondem.
- **16/16 testes** ao fim da M2.1. [Evidências e hipóteses da M2.1](docs/M2.1-STATUS.md).

## M2.0 — infraestrutura de boot do PS2 original

- **Loader de BIOS para o seu dump de ROM0:** exatamente 4 MiB; ROMDIR, EXTINFO e ROMVER (versão, região, tipo de console, data de build) validados campo a campo; identidade por SHA-256 com `--expect-sha256` opcional. ROM1/ROM2/EROM/NVM ainda não são carregados, então revisões nunca são misturadas. Nada da Sony é distribuído.
- **Barramento e MMU do EE:** KSEG0/KSEG1 decodificam o barramento físico (RAM de 32 MiB, ROM0 somente leitura em `0x1FC00000`); kuseg/KSEG2/KSEG3 só pela TLB de 48 entradas escrita pelo convidado (page mask, ASID, global, valid/dirty, bit S do scratchpad de 16 KiB). Toda região de dispositivo do EE (timers, DMAC, INTC, SIO, SIF, MCH, GIF, VIF, IPU, VU, GS, IOP) falha com seu nome; falhas de TLB são informadas pelo tipo de exceção.
- **Reset do EE e COP0:** PC `0xBFC00000`, `Status = BEV | ERL`, `PRid 0x2E20`, `Config 0x440`; `MFC0/MTC0` com regras por registrador, `TLBWI/TLBR/TLBP`, `ERET`, `EI/DI`, `CACHE` (só invalidação/write-back, pois a memória é coerente), `PREF`. Temporização de ciclos, conteúdo de cache, interrupções e vetorização de exceções não são modelados e param com diagnóstico.
- **Execução híbrida:** um único cabeçalho de semântica R5900 (`ee_semantics.hpp`) usado pelos blocos AOT e pelo novo interpretador. `ps2n-aot <rom0> <saida.cpp> --bios` traduz o código do firmware estaticamente alcançável e lista cada fronteira do interpretador; `ps2n-boot` executa blocos AOT quando o PC coincide exatamente e o interpretador nos demais casos, sempre visível no trace. Tradução de outra ROM0 é recusada.
- **Trace reproduzível:** eventos COP0/TLB/CACHE, trocas de motor, opcionalmente cada instrução e acesso de dados, e o ponto exato de bloqueio (PC, palavra, dispositivo, endereço físico).

Uma ROM0 sintética de autoria própria inicia no vetor de reset, passa por inicialização da TLB, mapeamento do scratchpad, invalidação de cache, cópia ROM→RAM, `ERET` e um payload em RAM, e para em `INTC_STAT` (`0x1000F000`), ainda não modelado: **582 instruções**, resultados idênticos com interpretador puro e híbrido (370 AOT + 213 interpretadas). **15/15 testes** aprovados localmente no Windows x86-64 (MSVC 19.44, LLVM clang++ 22). [Evidências da M2.0, uso com firmware real e limitações](docs/M2.0-STATUS.md).

```sh
./out/build/ps2n-boot /caminho/para/sua-rom0.bin --trace=full --trace-out=boot-trace.txt
```

## M1.3

- **Relocations MIPS como evidência, nunca aplicadas.** Em ELF32 `ET_EXEC` já vinculados que mantêm registros `SHT_REL`, `R_MIPS_26` (em `J`/`JAL` validados) e pares `R_MIPS_HI16`/`R_MIPS_LO16` (pareamento do ABI, vários HI16 por LO16, `LUI` + `%lo` com sinal) são conferidos contra os bytes vinculados e os símbolos. Um par HI16/LO16 que forma endereço de código (`ADDIU`) prova uma função cujo endereço é tomado. Registros malformados (HI16 órfão, instrução errada, símbolo inválido, sítio fora da seção de código) geram erro explícito; tipos não usados, como `R_MIPS_GPREL16`, são contados e informados, não descartados em silêncio.
- **Jump tables provadas pelo próprio código**, sem configuração por jogo: um `JR` só é aceito quando um caminho em linha reta mostra o limite `SLTIU`, o guarda `BEQ …, $zero`, o índice escalado por palavra, a base da tabela e o `LW` da entrada, sem desvio externo para dentro dessa janela. Formas GCC e ee-gcc são reconhecidas; toda entrada precisa ser código executável do mesmo segmento. `JR` não provados são informados com motivo.
- **Entradas indiretas verificadas em runtime:** `JR`/`JALR` só alcançam raízes de função, continuações de chamada ou alvos de tabelas provadas; o resto falha fechado.
- **Subconjunto inteiro do R5900** com semântica de 64 bits que preserva a metade alta do GPR: deslocamentos de 32/64 bits, `DADDU/DSUBU/DADDIU`, lógicas, `SLT*` de 64 bits, `MOVZ/MOVN`, `MULT/MULTU` de três operandos, `DIV/DIVU` (resultados definidos na divisão por zero), `MFHI/MFLO/MTHI/MTLO`, loads/stores de byte a quadword (`LQ/SQ`), `BLEZ/BGTZ/BLTZ/BGEZ`, `BxxZAL`, todas as formas branch-likely com anulação do delay slot e `SYNC`. `ORI` e `BEQ/BNE` passaram ao comportamento de 64 bits do R5900.

Um ELF sintético M1.3 (com e sem seções) é gerado, recompilado para C++, compilado nativamente e executado: **263 instruções**, 2 jump tables provadas, 61 valores de 64 bits de ALU/memória conferidos, registradores de link, anulação de branch-likely e destinos indiretos que falham fechados. **12/12 testes** aprovados localmente no Windows x86-64 (MSVC 19.44 e LLVM clang++ 22); os seis alvos são validados pelo [GitHub Actions](https://github.com/fabegalo/ps2-native/actions). [Evidências e limitações da M1.3](docs/M1.3-STATUS.md).

## M1.1

O recompilador AOT agora separa **decodificação MIPS, análise de fluxo e geração de C++**. Identifica funções por símbolos ELF32 `STT_FUNC` validados ou por entradas verificadas `--entry=0xADDRESS` em arquivos sem símbolos. Gera **funções nativas por bloco básico e uma tabela de despacho**: `JALR/JR` podem chamar funções recompiladas sem interpretar as instruções MIPS em tempo de execução.

Um ELF sintético executou **28 instruções**, três chamadas indiretas, preservando delay slots e registradores de retorno (inclusive `JALR rs==rd`). Alvos desconhecidos, código modificado na RAM e símbolos inválidos provocam erro em vez de sucesso falso. Foram aprovados **6/6 testes** no Linux x86-64 com GCC 14.2 e Clang 17 + ASan/UBSan. [Evidência e limites do M1.1](docs/M1.1-STATUS.md).

```sh
./out/build/ps2n-aot input.elf out/build/generated/output.cpp --entry=0x00100050
```

A opção informa uma função já identificada; **não transforma jogos comerciais em executáveis nativos automaticamente**.

## Base anterior — M0

Projeto independente inspirado na **visão** do [x360-native](https://github.com/fabegalo/x360-native): rodar o sistema original e jogos do PlayStation 2 em computadores, priorizando recompilação estática AOT e interfaces de runtime verificáveis. **O x360-native não foi modificado.**

## O que já funciona (M0)

- Validação e carregamento de `ELF32`, little-endian, MIPS `ET_EXEC`, com segmentos `PT_LOAD`, endereços e BSS; outras variantes são rejeitadas explicitamente.
- Modelo inicial de RAM EE de 32 MiB, com espelhos KSEG0/KSEG1, operações de 8 e 32 bits, acesso alinhado e erros para hardware não implementado.
- Estrutura de 32 registradores de 128 bits do R5900, ainda **sem semântica MMI/128 bits**; apenas operações de palavra de um subconjunto de instruções.
- Descoberta conservadora de instruções alcançáveis a partir de `e_entry` por fluxo de controle direto; salto indireto para destino não descoberto falha explicitamente.
- Geração de C++20 por AOT e **compilação real desse código pelo compilador host**. Não é um interpretador oculto.
- Teste end-to-end: ELF MIPS sintético → código C++ AOT → executável nativo → 37 instruções → `$v0=37`, memória `[0x40]=37` e retorno após delay slot.
- Conjunto suportado: `NOP`, `ADDIU`, `ADDU`, `ORI`, `LUI`, `LW`, `SW`, `BEQ`, `BNE`, `J`, `JAL`, `JR` sob restrições explicitadas em `docs/M0-STATUS.md`.
- Operações não implementadas geram erro em vez de falso sucesso.

Nenhum arquivo proprietário, biblioteca GPL, BIOS ou jogo faz parte desta base.


## M1.2 — destinos indiretos com evidência verificável

A descoberta de jump tables é conservadora: o parser ELF32 identifica ponteiros `R_MIPS_32` **já resolvidos** em seções de dados não executáveis, associados a símbolos `STT_SECTION` ou `STT_FUNC` em código executável. Os destinos válidos entram no grafo AOT, inclusive endereços internos da seção sem símbolo de função. Candidatos rejeitados têm diagnóstico explícito.

Em executáveis sem relocations/símbolos, é possível informar uma tabela conhecida: `ps2n-aot entrada.elf saida.cpp --table=0x00101000:2`. Todos os elementos são validados; tabelas inválidas falham. Endereços indiretos não descobertos continuam rejeitados. **Isso não representa compatibilidade genérica com jogos, overlays ou todas as variantes de relocação MIPS.** Veja o [status M1.2](docs/M1.2-STATUS.md).

## Compilar e testar

Requisitos: CMake 3.21+, compilador C++20, Ninja (Linux/macOS; também recomendado no Windows). Não requer Python, administrador ou dependências externas.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

No Windows, alternativamente use o gerador Visual Studio e `cmake --build out/build --config Release` (a CI usa MSVC no Windows).

O build gera seu próprio ELF de teste em `out/build/fixtures/synthetic.elf` e recompila para `out/build/fixtures/synthetic_aot.cpp`. A geração integra o grafo do CMake: nenhum executável do jogo é distribuído e uma nova geração não é feita quando a entrada não muda.

### Ferramenta AOT para análise técnica

```sh
./out/build/ps2n-aot ./meu_programa_mips.elf ./out/build/generated/program.cpp
```

**Importante:** o comando **só gera C++**. Esse C++ precisa ser compilado e conectado a um host que inicialize a memória e forneça o contexto EE. O fluxo automático completo existe apenas para o fixture sintético neste M0. Ele não é um conversor universal de jogos PS2. Gere apenas a partir de arquivos que você tem direito de usar; não redistribua código traduzido de jogos comerciais.

## Arquitetura

- `include/ps2native/io.hpp` / `src/io.cpp`: fronteira única de I/O host, caminhos nativos Unicode e escrita incremental.
- `include/ps2native/elf.hpp` / `src/elf.cpp`: contrato de validação ELF.
- `include/ps2native/memory.hpp` / `src/memory.cpp`: RAM/espelhos e acesso seguro.
- `include/ps2native/ee.hpp` / `src/ee.cpp`: estado EE sem estado global.
- `include/ps2native/mips.hpp` / `src/mips.cpp`: decodificador do subconjunto R5900.
- `include/ps2native/control_flow.hpp` / `src/control_flow.cpp`: análise de funções e fluxos.
- `include/ps2native/aot.hpp` / `src/aot.cpp`: geração AOT e despachante.
- `include/ps2native/aot_runtime.hpp`: ponto de entrada da tradução compilada.
- `tools/ps2n-aot.cpp`: CLI de geração.
- `tests/`: firmware/jogos **não** são necessários; fixture MIPS de autoria própria.
- `.github/workflows/build.yml`: matriz dos seis sistemas/arquiteturas, sem releases prematuras.

A API permanece uniforme por plataforma. Consultar [M2.2](docs/M2.2-STATUS.md), [M2.1](docs/M2.1-STATUS.md), [M2.0](docs/M2.0-STATUS.md), [M1.3](docs/M1.3-STATUS.md), [M1.2](docs/M1.2-STATUS.md), [M1.1](docs/M1.1-STATUS.md), [arquitetura](docs/ARCHITECTURE.md), [auditoria das referências](docs/REFERENCE-AUDIT.md), [status técnico](docs/M0-STATUS.md) e [licenciamento](docs/LICENSE-DECISION.md).

## Próximos marcos

**M1:** ampliar semântica R5900, resolver fluxos indiretos/relocações e criar despachante verificável com fallback explícito. M2–M6: MMU/IOMMU, IOP, VU, DMA, GS, SPU2, BIOS/Browser originais, jogos, launcher e otimizações multiplataforma. Não existe atalho legítimo que elimine a necessidade desses componentes.

**Licença:** BSD-3-Clause apenas para o código original deste protótipo. Código GPL de projetos de referência não foi incorporado. Consulte `docs/LICENSE-DECISION.md` antes de integrar bibliotecas de terceiros.

Próxima tarefa objetiva: [continuação da M2.2](docs/NEXT-TASK.md).
