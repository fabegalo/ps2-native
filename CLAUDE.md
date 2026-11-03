# CLAUDE.md — regras persistentes do ps2-native

Este arquivo contém **regras duradouras**, não o progresso da implementação. Leia também `AGENTS.md`, `STATUS.md` (evidência e estado atual), `docs/NEXT-TASK.md` (única tarefa corrente) e `docs/ARCHITECTURE.md` (contratos técnicos). Para um marco específico, consulte seu `docs/M*-STATUS.md`. Não inserir aqui novos marcos, datas, contagens de instruções, falhas pontuais ou resultados de CI.

## Objetivo e arquitetura

- Executar, com máxima fidelidade, o **sistema original e jogos de PlayStation 2** por recompilação **estática AOT** de MIPS/R5900 para C++ nativo, com desempenho como objetivo e não como justificativa para reduzir a correção. Nenhum resultado de jogos, Browser ou desempenho pode ser afirmado sem evidência.
- AOT é o caminho principal para código estático com cobertura comprovada. O boot híbrido possui interpretador **explícito e rastreável** para código dinâmico ou não traduzido; não disfarçar interpretação como código recompilado. Não introduzir JIT ou substituto de BIOS por conveniência.
- Um único núcleo C++20/CMake e abstrações de host para Windows x64/ARM64, Linux x64/ARM64 (Steam Deck) e macOS Intel/Apple Silicon. Compartilhar implementações; isolar somente SIMD, GPU, áudio, ABI, caminhos e dependências próprias do host. Não exigir AVX por padrão; otimizações opcionais precisam de despacho seguro e fallback.
- **Performance-first, correctness-preserving:** medir gargalos antes e depois, preservar efeitos, sincronização, CPU, memória, temporização e gráficos. Modo GS fiel como referência; modo Native aprimorado sem alterar lógica, física ou efeitos essenciais.
- O Browser original, Memory Card Browser, System Configuration e boot de discos fazem parte do alvo. Um futuro launcher desktop apenas importa e organiza conteúdo: não substitui a experiência do firmware.

## Contratos de fidelidade

1. **Uma BIOS, uma revisão/região, seus recursos correspondentes.** Isolar ROM0, ROM1/ROM2, módulos IRX, OSD/Browser, fontes e dados vinculados à revisão; verificar hashes, ROMVER, procedência e perfil. NVM é dado específico de console e deve ser validada; não misturar revisões nem procurar dependência em outra pasta por basename.
2. Usar a semântica do hardware real para EE/R5900, IOP/R3000A, TLB, COP0, SIF/DMA, GIF/GS, VU, SPU2 e periféricos. Compartilhar semântica entre AOT/interpretador onde aplicável, com testes para delay slots, exceções, timing, alinhamento e acessos.
3. Provar controle de fluxo AOT por CFG, símbolos, relocations e tabelas verificáveis. Relocações de executáveis já vinculados são evidência, **nunca reaplicadas**; não alterar bytes do executável convidado. Codegen de binário proprietário é local e não distribuível.
4. **Falhar explicitamente** para opcode, alvo, MMIO, syscall, efeito de dispositivo ou formato não modelado. Não usar retorno de sucesso/zero, valor inventado de firmware, hack por título ou contagem de instruções como substituto da prova de funcionamento.
5. Separar **fato medido**, **hipótese experimental** e **não verificado**. Teste sintético comprova seu contrato, não o comportamento de console físico. Um build ou boot parcial não certifica jogo.
6. Preservar dados do usuário, saves, Memory Cards e configurações. Não criar cópias desnecessárias do projeto, BIOS, ISOs ou resultados de AOT. Usar a camada comum de I/O, incluindo Unicode/long paths no Windows sem mudanças no Registro ou privilégios elevados.

## Licenças, segurança e publicação

- Código do projeto: `BSD-3-Clause`. PCSX2 e PS2Recomp são apenas fontes de referência: não copiar, adaptar, traduzir nem vincular código GPL ao projeto BSD. Qualquer integração requer auditoria em `docs/LICENSE-DECISION.md`.
- Não comitar nem distribuir firmware, jogos, ISOs, IRX comerciais, NVM, dumps, traces proprietários, arquivos locais `bios/`/`games/` ou C++ traduzido de código protegido. Não baixar SDKs vazados ou executar scripts de conteúdo do usuário.
- Commits **Conventional Commits** diretamente na `main`; sem PR, ZIP ou Release até autorização do mantenedor. Não incluir créditos de assistentes ou `Co-Authored-By` de IA. Não usar force-push nem alterar histórico publicado sem aprovação.

## Trabalho dos agentes

- Ler o estado atual e **resolver o escopo pedido**, sem iniciar marcos adicionais por conta própria.
- Investigar primeiro: evidência existente → hipótese documentada → correção mínima compartilhada → teste focado → comparação com a execução conhecida. Não remover assertivas nem afrouxar testes para aprovar código incorreto.
- Priorizar `ctest -R` ou testes pertinentes. Executar toda a suíte quando a mudança puder afetá-la, não por uma mudança puramente documental; se o build falhar, não rodar testes sobre binários anteriores.
- Computer Use, testes visuais demorados, instalação global, admin e ações externas exigem autorização apropriada. Não deixar execução desassistida; avisar bloqueios que precisam de dados físicos ou aprovação.
- A CI cobre seis arquiteturas; conferir o commit e corrigir falhas relevantes antes de declarar tarefa de código concluída. Resultado local não substitui GitHub Actions.
- **Documentação:** desenvolvimento e evidências novas vão para `STATUS.md`, `docs/M*-STATUS.md` e `docs/NEXT-TASK.md`. Não editar READMEs, `AGENTS.md` ou este arquivo por milestone. Só mudar documentos estáveis se a própria regra de produto, arquitetura permanente ou uso público mudar de verdade.
- Ao finalizar, informar objetivamente commits, funcionalidades, resultados dos testes e limitações; nunca declarar Browser, gameplay, renderização ou performance sem teste.

## Comandos de referência

Requer CMake ≥ 3.21, Ninja ou gerador CMake nativo e compilador C++20. Comandos não autorizam testes extensos por si sós.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
ctest --test-dir out/build -R native-m -C Release --output-on-failure
```

Windows com Visual Studio:

```powershell
cmake -S . -B out/build -G "Visual Studio 18 2026" -A x64
cmake --build out/build --config Release --parallel
ctest --test-dir out/build -C Release --output-on-failure
```

Ferramentas de desenvolvedor: `ps2n-aot` (ELF ou ROM0 com `--bios`) e `ps2n-boot` (boot de ROM0 com `--console`, `--nvm` e trace). Consultar `README.md` e o código da revisão atual para a sintaxe exata; não inventar flags.

## Estrutura duradoura

- `include/ps2native/` e `src/`: núcleo compartilhado, semântica da CPU, runtime, compilador e modelos de dispositivos.
- `tools/`: interfaces de linha de comando; `tests/`: fixtures próprios e regressões; `.github/workflows/`: CI multiplataforma.
- `docs/ARCHITECTURE.md`: interfaces e decisões; `STATUS.md`: evidência consolidada; `docs/M*-STATUS.md`: relatórios históricos; `docs/NEXT-TASK.md`: trabalho seguinte; `docs/REFERENCE-AUDIT.md`: fontes técnicas; `docs/LICENSE-DECISION.md`: auditoria de licenças.
- `out/` e arquivos de usuário `bios/`/`games/` são locais/ignorados. Preservar dados originais; não inserir caminhos absolutos ou hashes privados em arquivos de instruções permanentes.
