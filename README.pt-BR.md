# ps2-native — Recompilação nativa do PlayStation 2
**Idiomas:** [English](README.md) | Português (Brasil)


> **Status:** protótipo experimental M1.1. Ainda não é um emulador PS2, um launcher, nem executa a BIOS ou jogos comerciais.

## Novidades da M1.1

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

A API permanece uniforme por plataforma. Consultar [M1.1](docs/M1.1-STATUS.md), [arquitetura](docs/ARCHITECTURE.md), [auditoria das referências](docs/REFERENCE-AUDIT.md), [status técnico](docs/M0-STATUS.md) e [licenciamento](docs/LICENSE-DECISION.md).

## Próximos marcos

**M1:** ampliar semântica R5900, resolver fluxos indiretos/relocações e criar despachante verificável com fallback explícito. M2–M6: MMU/IOMMU, IOP, VU, DMA, GS, SPU2, BIOS/Browser originais, jogos, launcher e otimizações multiplataforma. Não existe atalho legítimo que elimine a necessidade desses componentes.

**Licença:** BSD-3-Clause apenas para o código original deste protótipo. Código GPL de projetos de referência não foi incorporado. Consulte `docs/LICENSE-DECISION.md` antes de integrar bibliotecas de terceiros.

Próxima tarefa objetiva: [M1.2](docs/NEXT-TASK.md).
