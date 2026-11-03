# CLAUDE.md — contexto persistente do ps2-native

Leia também `README.md` / `README.pt-BR.md`, `docs/ARCHITECTURE.md` (contratos
implementados e interfaces propostas), o status do marco mais recente
(`docs/M1.3-STATUS.md`, com `docs/M1.2-STATUS.md`, `docs/M1.1-STATUS.md` e
`docs/M0-STATUS.md` como histórico) e `docs/NEXT-TASK.md`. Licenças e referências estão em
`docs/LICENSE-DECISION.md` e `docs/REFERENCE-AUDIT.md`. Atualizar status e
READMEs (inglês e português) somente com o que foi demonstrado por teste.

## Objetivo

Executar jogos e o sistema original do PlayStation 2 nativamente em PC por
**recompilação estática AOT**, com compatibilidade e desempenho iguais ou
superiores ao PCSX2 como metas de longo prazo, nunca como afirmação. Projeto
independente que compartilha a *visão* do x360-native; **não modificar o
x360-native** nem herdar decisões específicas do Xbox 360 que não sirvam ao
hardware da Sony.

AOT é o caminho principal para código estático identificado com segurança.
Overlays, módulos dinâmicos e partes do firmware poderão usar fallback
interpretado/dinâmico **explícito e verificável**, ainda não implementado;
hoje, o que não foi recompilado falha com erro claro.

Uma única base C++20/CMake para seis alvos: Windows x86-64/ARM64, Linux
x86-64/ARM64 (Steam Deck incluído) e macOS Intel/Apple Silicon. Isolar apenas o
que depende da plataforma (SIMD, GPU, áudio, memória executável, toolchain,
ABI); não criar cópias do projeto, de ISOs, SDKs ou builds por sistema.

**Performance-first, correctness-preserving** é política permanente: toda
otimização exige medição antes/depois em condições equivalentes, sem reduzir
fidelidade, ocultar erros, remover sincronização necessária ou alterar
semântica R5900, delay slots, memória e temporização. O modo Native nunca altera
física, lógica, temporização ou renderização para ganhar FPS; o modo GS fiel é a
referência de correção.

Marcos: M0 fundação → M1 Generic AOT → M2 sistema PS2 (EE, IOP, DMA, VU,
firmware) → M3 GS/SPU2 e primeiros frames → M4 Browser original → M5 jogos →
M6 multiplataforma e otimizações. Reavaliar conforme dependências técnicas
reais; não forçar Browser ou jogos com stubs.

## Estado atual (2026-11-03)

- M0, M1.1, M1.2 e M1.3 concluídos. 12 testes CTest (ver `docs/M1.3-STATUS.md`).
- M1.3: relocations `R_MIPS_26` e `R_MIPS_HI16/LO16` lidas como evidência
  (nunca aplicadas), jump tables provadas por `SLTIU` + `BEQ $zero`, entradas
  indiretas verificadas em runtime e subconjunto inteiro R5900 de 64 bits.
- Próximo incremento: **M1.4** (`docs/NEXT-TASK.md`) — fronteira explícita de
  exceções/syscalls do EE e loads/stores desalinhados, sem kernel falso.
- Fixtures são somente sintéticos. Nenhum jogo comercial, BIOS, Browser, IOP,
  VU, GS, SPU2 ou DMA é suportado. Nenhum jogo é declarado compatível.

## Fidelidade ao console — regra permanente

O comportamento deve ser o mais fiel possível ao PS2 original, idealmente
idêntico: animação de boot, Browser, System Configuration, Memory Card Browser,
seleção e inicialização de discos, códigos de erro e serviços da BIOS da
revisão correspondente. O launcher desktop gerencia conteúdo, configuração e
execução; **não substitui** menus originais por interfaces falsas. Divergência
de projeto é corrigida em direção ao original, só permanece com justificativa
registrada, e o que não foi comprovado no original fica marcado como hipótese.
Sem sistemas de migração enquanto não houver release: dados locais de teste
são descartáveis. Saves, Memory Cards e configurações reais do usuário são
sempre preservados.

## Isolamento de BIOS e firmware — regra arquitetural permanente

**Uma BIOS, seus componentes correspondentes. Sem mistura de versões ou
regiões.** Cada revisão/região de BIOS (e seus módulos IRX, ROM1/ROM2, fontes,
OSD/Browser e recursos) usa apenas os próprios arquivos. Nomes de módulo,
exports ou tamanhos iguais não comprovam compatibilidade. Registrar versão,
região, procedência e SHA-256 por perfil; recusar dependências ausentes ou
divergentes antes da execução, com mensagem identificando versão e arquivo
necessários. Proibido fallback silencioso, busca por basename em outra versão
ou cache global de componentes originais. Infraestrutura genérica (EE, IOP,
VU, GS, I/O) pode ser compartilhada; firmware específico de uma revisão, não.
O mesmo vale para jogos: perfis por título só para diferenças comprovadas,
indexados por serial/região/versão e hash.

## Licenças — regra permanente

- Código original do projeto é **BSD-3-Clause** (`LICENSE`).
- PCSX2 (GPL-3.0+) e PS2Recomp (GPL-3.0) são referências de comportamento e
  arquitetura: **não copiar, derivar, ligar ou traduzir código deles**. Play!
  e N64Recomp têm licenças permissivas, mas qualquer integração exige revisão
  caso a caso. Antes de integrar componente incompatível, apresentar a decisão
  de licenciamento ao mantenedor e registrá-la em `docs/LICENSE-DECISION.md`.
- Dependências novas exigem versão e hash fixados (actions por SHA de commit).
- Nunca adicionar BIOS, firmware, jogos, ISOs, ELFs/IRX comerciais, assets,
  saves ou C++ traduzido de binários proprietários. Conteúdo do usuário fica
  fora do repositório (ver `.gitignore`); não baixar conteúdo de terceiros nem
  executar scripts vindos de dados de jogos.

## Commits e publicação — regra permanente

- Trabalhar diretamente na `main`, salvo instrução contrária do mantenedor.
  Sem PR, sem ZIP e sem Release até existir produto executável e validado.
- Conventional Commits: `tipo(escopo): descrição objetiva`, com tipo pelo
  conteúdo real (`feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`,
  `ci`, `chore`). Exemplos: `feat(aot): pair R_MIPS_HI16/LO16 relocations`,
  `test(elf): reject orphan HI16 relocation`.
- Não incluir referências a Claude, Codex ou outros assistentes como autores no
  título, corpo ou rodapés: sem “Generated with...”, sem `Co-Authored-By`
  atribuído a assistente. Preservar créditos humanos, licenças e avisos de
  terceiros.
- Nunca reescrever histórico publicado nem fazer force-push sem autorização
  expressa. Push só quando o trabalho estiver pronto e a tarefa o pedir;
  depois do push, acompanhar a CI dos seis alvos e corrigir falhas antes de
  declarar a tarefa concluída.

## Trabalho dos agentes — regras permanentes

- Computer Use e sessões visuais/longas exigem autorização explícita prévia.
- Priorizar testes focados (`ctest -R <nome>`); rodar a suíte completa quando a
  mudança puder afetá-la (hoje ela é curta). Não repetir validações aprovadas
  por uma mudança puramente documental.
- Nenhum processo em segundo plano sem acompanhamento e limite de duração.
- Se uma ferramenta exigir interação não autorizada, interromper e informar o
  bloqueio imediatamente; não esperar horas por aprovação.
- Não ampliar o escopo nem começar outro marco antes de concluir o atual.
- Ao finalizar uma tarefa, informar apenas: commits, funcionalidades
  implementadas, testes/CI e limitações restantes.

## Comandos (raiz do repositório)

Requer CMake ≥ 3.21, Ninja e compilador C++20; sem bibliotecas externas.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
ctest --test-dir out/build -R native-jump --output-on-failure   # teste focado
```

Windows (igual à CI; Visual Studio 2026):

```powershell
cmake -S . -B out/build -G "Visual Studio 18 2026" -A x64
cmake --build out/build --config Release --parallel
ctest --test-dir out/build -C Release --output-on-failure
```

```sh
./out/build/ps2n-aot input.elf out.cpp [--entry=0xADDR]... [--table=0xADDR:COUNT]...
```

- Se o build falhar, **não rode os testes** sobre binários antigos.
- CI: `.github/workflows/build.yml`, matriz de seis alvos, `fail-fast: false`.
  Resultado local não substitui a CI; confirmar os jobs do commit
  correspondente (`gh run list`, `gh run view`) antes de declarar aprovação.
- Resultado de referência (M1.3): 12/12 testes — `core-validation`,
  `native-aot-smoke`, `native-aot-ops`, `native-indirect-{sym,hints,unresolved}`,
  `native-jump-{relocations,explicit,unresolved}`,
  `native-m13-{relocations,stripped,stripped-hint}`. Qualquer falha é regressão.

## Estrutura

- `include/ps2native/` — contratos públicos: `elf`, `memory`, `ee`, `mips`,
  `control_flow`, `aot`, `aot_runtime`, `io`, `core`.
- `src/` — biblioteca `ps2n_core`: loader ELF32 (`elf.cpp`), análise de
  relocations sem aplicá-las (`relocations.cpp`), RAM EE de 32 MiB e espelhos
  KSEG0/1 (`memory.cpp`), estado EE (`ee.cpp`), decodificador MIPS
  (`mips.cpp`), CFG e descoberta de funções (`control_flow.cpp`), emissor C++
  e runtime de despacho (`aot.cpp`), I/O centralizado Unicode/long paths
  (`io.cpp`). Todo novo `.cpp` entra em `ps2n_core` no `CMakeLists.txt`.
- `tools/ps2n-aot.cpp` — CLI AOT; reusa o `.cpp` gerado se idêntico
  (`write_text_if_changed`), nunca copia o ELF de entrada.
- `tests/` — `make_fixture.cpp` gera os ELFs sintéticos no build (modo por
  argumento); `m13_program.hpp` é o mini-assembler com rótulos + construtor
  ELF compartilhado entre gerador e teste (endereços definidos uma vez); cada
  teste E2E compila a TU gerada com `ps2n_core` e valida registradores, RAM,
  contagem de instruções e falhas esperadas.
- `docs/` — arquitetura, status por marco, próxima tarefa, auditoria de
  referências e decisão de licença.
- `out/`, `build/`, `generated/` — ignorados; artefatos locais.

## Regras

1. Não inventar opcodes, semântica, tipos de relocation, flags, endereços,
   hashes ou resultados. Confirmar em especificação MIPS/R5900, no comportamento
   observável do PCSX2 (sem copiar código) ou em teste próprio.
2. Marcar fato, hipótese e não verificado. "Compilou" ≠ "executa" ≠ "inicia" ≠
   "jogável". Compatibilidade de jogo só com teste real de gameplay.
3. Não esconder função ausente retornando sucesso/zero. Sem stubs falsos, hacks
   por jogo ou fallback silencioso: opcode, relocation, delay slot, destino
   indireto, MMIO ou syscall não suportado gera erro explícito.
4. Descoberta de código só por evidência verificável (símbolos, relocations,
   tabelas validadas, fluxo direto). Sem varredura especulativa de `.text` ou
   dados. Hints de CLI (`--entry`, `--table`) são validados e nunca configuração
   por jogo embutida no código.
5. Não reaplicar relocations em executáveis já vinculados (`ET_EXEC`); usá-las
   apenas como evidência. Nenhum byte do ELF de entrada é alterado.
6. Preservar semântica R5900: delay slots, `$zero`, registradores de 128 bits,
   comportamento de 32/64 bits e exceções. Código modificado em RAM é detectado
   e rejeitado até existir fallback explícito.
7. Não remover verificações nem afrouxar testes. Toda funcionalidade nova traz
   fixture sintético e teste CTest, inclusive casos de erro.
8. Para corrigir: reprodução focada → hipótese → mudança pequena → teste
   relevante. Uma única implementação comum; sem duplicação de lógica.
9. Não afirmar desempenho sem medir e descrever a medição.
10. Sem instalação global, alteração do Registro do Windows ou admin sem
    explicar e pedir.
