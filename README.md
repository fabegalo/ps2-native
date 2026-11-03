# ps2-native — M0 (prova técnica)

> **Status:** protótipo experimental. Ainda não é um emulador PS2, um launcher, nem executa a BIOS ou jogos comerciais.

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
- `include/ps2native/aot.hpp` / `src/aot.cpp`: CFG limitado e emissão AOT.
- `include/ps2native/aot_runtime.hpp`: ponto de entrada da tradução compilada.
- `tools/ps2n-aot.cpp`: CLI de geração.
- `tests/`: firmware/jogos **não** são necessários; fixture MIPS de autoria própria.
- `.github/workflows/m0-build.yml`: matriz dos seis sistemas/arquiteturas, sem releases prematuras.

A API permanece uniforme por plataforma. Consultar [arquitetura](docs/ARCHITECTURE.md), [auditoria das referências](docs/REFERENCE-AUDIT.md), [status técnico](docs/M0-STATUS.md) e [licenciamento](docs/LICENSE-DECISION.md).

## Próximos marcos

**M1:** ampliar semântica R5900, resolver fluxos indiretos/relocações e criar despachante verificável com fallback explícito. M2–M6: MMU/IOMMU, IOP, VU, DMA, GS, SPU2, BIOS/Browser originais, jogos, launcher e otimizações multiplataforma. Não existe atalho legítimo que elimine a necessidade desses componentes.

**Licença:** BSD-3-Clause apenas para o código original deste protótipo. Código GPL de projetos de referência não foi incorporado. Consulte `docs/LICENSE-DECISION.md` antes de integrar bibliotecas de terceiros.

Próxima task objetiva para o agente: [M1.1](docs/NEXT-TASK.md).
