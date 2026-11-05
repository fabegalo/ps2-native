# AOT: memória e eventos de hardware — 05/11/2026

## Referência e seleção

Consulta somente de leitura do PS2Recomp, revisão
`e5636981a1c9de368f6b71e819c3dc188dfedbf4`. Implementações próprias BSD;
nenhum arquivo, macro, algoritmo numérico ou trecho GPL foi integrado,
adaptado ou vinculado. O objetivo foi identificar decisões de organização
da execução que pudessem ser provadas nos contratos existentes.

| Caminho | PS2Recomp consultado | ps2-native antes desta mudança | Decisão |
|---|---|---|---|
| Geração e despacho | Funções C++ com labels internos, retomada e transferências diretas | `src/aot.cpp`, `src/aot_runtime.cpp`, `src/boot.cpp`: funções por bloco, entradas interiores e encadeamento de delay slots já implementados | Preservar guardas e cobertura; o despacho exclusivo instrumentado anterior custava 0,248 s em 30 milhões, abaixo dos outros candidatos |
| Tradução e memória | Tradutor escolhe acessos por largura e endereço conhecido; runtime possui cópias escalares especializadas | `src/memory.cpp`: cada fetch de 32 bits passava pelo leitor genérico de dados, com seleção de cache, dispositivo, largura, metadados e observação | Especializar fetch após a resolução completa; nenhuma suposição estática sobre RAM substitui a TLB |
| Cache e invalidação | Helpers de ponteiros e acessos rápidos pertencem ao modelo de memória daquele runtime | `src/memory_cache.cpp`: D-cache funcional, proveniência, DMA e writeback; bytes executáveis são conferidos por instrução | Manter dados e conhecimento atuais em cada fetch; nenhuma cache de bytes AOT ou invalidação por página foi introduzida |
| Scheduler | Próximos deadlines e checkpoints de execução, juntamente com serviços de threads/kernel do runtime | `src/boot.cpp` e `ee_semantics.hpp` preservam fronteiras por instrução; sincronização chama repetidamente os mesmos motores VU/VIF/DMA | Aplicar apenas ausência comprovada de evento nos motores de hardware; manter kernel convidado, ordem e timestamps |
| FPU/VU | Macros com operações host, caminhos especiais e SIMD; tradutores chamam esses helpers | `ee_semantics.hpp` compartilha semântica, flags e domínios numéricos certificados com a interpretação | Nenhuma troca aritmética: equivalência dos domínios, flags, truncamentos e temporização não decorre de usar float/SIMD |
| Constantes e saída | Estado de constantes no emissor; saída combinada ou por função, workers de geração | Constantes de operandos/GPR já especializadas; uma TU comercial com corpos únicos e muitas entradas | Sem nova duplicação; particionamento e propagação adicional ficam condicionados a outra medição |

Fontes primárias fixadas: [emissor de funções](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/function_emitter.cpp),
[fluxo](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/control_flow_emitter.cpp),
[tradutor](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/instruction_translator.cpp),
[memória](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/ps2_memory.cpp),
[ponteiros](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/include/runtime/ps2_memory.h),
[macros](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/include/ps2_runtime_macros.h),
[FPU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/fpu_translator.cpp),
[VU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/vu_translator.cpp),
[scheduler](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp)
e [saída paralela](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/ps2_recompiler.cpp).

O perfil anterior da mesma janela tinha 4,834 s exclusivos em memória,
6,510 s em sincronização e 5,804 s em dispositivos, com 62.813.598 chamadas
de sincronização. São tempos instrumentados, não parcelas dos tempos sem
profiling abaixo. Por isso as duas primeiras candidatas foram memória e
trabalho redundante dos dispositivos, antes de ampliar funções geradas.
Uma coleta complementar do binário de baseline congelado confirmou
4,842 s em memória, 6,509 s em sincronização, 5,791 s em dispositivos,
2,058 s em VU e 0,248 s em despacho; mesmo estado completo e contagens.

## Contratos implementados

1. `Memory::fetch32`: resolve privilégios, alinhamento, ASID/TLB e região
   em cada acesso. RAM, scratchpad, ROM0 e ROM1 usam leitura especializada
   de quatro bytes. DMA que escreve RAM é sincronizado antes da leitura;
   bits desconhecidos continuam recusados. Fetch conserva sua visão de
   backing RAM, sem consultar D-cache nem emitir observação de dados.
   MMIO/VU continuam no caminho de recusa existente. O guard AOT continua
   comparando o código atual em cada instrução, incluindo após stores,
   writeback, DMA e callbacks.
2. `VuControl::advance_micro`: retorna antes do trabalho do motor somente
   se nenhum evento micro, PATH1 ou Q de qualquer unidade estiver vencido.
   Q continua completando após E terminar o microprograma. VIF só entra no
   drain se há entrada ou flags de espera para resolver. DMA gráfico inativo
   atualiza o timestamp e preserva a ordem das recusas sem entrar no parser.
   Canais ativos continuam usando o mesmo parser, arbitragem e relógio.

Não foram agrupados ciclos EE/IOP nem removidas amostragens de IRQ,
verificações de bytes, síntese, conhecimento numérico ou checkpoints.
Nenhuma alteração de FPU/VU aritmética, SIMD obrigatório ou `/fp:fast`.

## Método das medições

Windows x64, Ryzen 7 5700X3D, MSVC 19.44.35229, C++20,
`/MD /EHsc /O2 /bigobj /DNDEBUG /utf-8`, profiling desativado.
Baseline congelado antes das duas candidatas, mantendo as mesmas mudanças
locais preexistentes de host/IOP, sem callbacks de entrada conectados.
Sem compilação concorrente durante benchmarks. Entradas soltas, mesmos
ROM0/ROM1, perfil, NVM, disco, raízes explícitas e módulo idle verificados
do [benchmark anterior](AOT-PERFORMANCE-STATUS.md).

Janelas: `realignment-before-sqrt.ps2ncp` (um milhão),
`superbust-realignment-game2-latest.ps2ncp` (30 milhões) e
`superbust-before-vu-underflow-exact.ps2ncp` (30 milhões com síntese ativa).
Carga, hash final e salvamento ficam fora do tempo; execução e relatório
comum ficam dentro. RGBA inclui dimensões e ciclos; PCM inclui núcleo,
ciclo e os dois canais de cada amostra. Artefatos comerciais ficam locais.

## Resultados pareados

Código: `6f3f968` (fetch) e `ac7e405` (eventos). Cinco rodadas por
variante/janela, alternando a ordem baseline/fetch/eventos/combinação e
sua inversa. A tabela usa medianas dos tempos, sem selecionar o melhor
ensaio. As quatro variantes mantiveram os mesmos hashes completos nas
40 execuções e cobertura AOT de 99,0868% e 85,979867% respectivamente.

| Variante isolada ou combinada | 1 milhão: s / milhões de instruções/s | Ganho de throughput | 30 milhões: s / milhões de instruções/s | Ganho de throughput |
|---|---|---:|---|---:|
| Baseline atual reensaiado | 0,364409 / 2,744 | — | 10,492100 / 2,859 | — |
| Fetch especializado | 0,354868 / 2,818 | +2,69% | 10,095000 / 2,972 | +3,93% |
| Eventos VU/VIF/DMA | 0,315132 / 3,173 | +15,64% | 9,004090 / 3,332 | +16,53% |
| Duas mudanças | 0,313884 / 3,186 | **+16,10%** | 8,920160 / 3,363 | **+17,62%** |

Na janela longa, o tempo diminuiu 14,98%. Mediana dos 30 intervalos
entre campos reais: baseline 1,714135 s, fetch 1,651575 s, eventos
1,468245 s e combinação **1,455465 s**. Os sete campos por execução
usam conteúdo original; isto continua longe de apresentação em tempo real.
O fetch também reduziu o tempo longo nas cinco comparações individuais.
Depois dos eventos, seu benefício adicional longo é pequeno, mas apareceu
nos cinco pares; na janela curta não há ganho adicional consistente que
justifique somar os percentuais das otimizações isoladas.

Intervalos das cinco amostras longas: baseline 10,4451–10,7207 s,
fetch 10,0638–10,3651 s, eventos 8,98701–9,07106 s e combinação
8,88421–8,93552 s. Na janela curta: baseline 0,359140–0,388898 s
e combinação 0,306626–0,316515 s.

O baseline fornecido anteriormente foi 2,804/2,854 milhões de instruções/s,
1,719560 s por campo. O novo controle mediu 2,744/2,859 e 1,714135 s:
os ganhos acima usam o controle contemporâneo pareado, não os números
históricos como denominador. Aquecimento e dispersão afetam especialmente
a janela curta. Logs e todas as amostras estão no resumo local, inclusive
as rodadas exploratórias anteriores, excluídas da tabela de cinco pares.

Nenhuma função ou corpo AOT foi adicionado/duplicado. O executável passou
de 11.771.392 para **11.771.904 bytes** (+512 bytes, 11,2265625 MiB).
A recompilação integral das mesmas três TUs e link levou **99,204591 s**;
o registro histórico era 89,093873 s. Não é comparação pareada de builds
nem evidência de ganho de compilação. O gerador e seus arquivos comerciais
permaneceram idênticos; `.text` do executável recompilado integralmente
coincidiu com a combinação medida antes, por SHA-256. O custo anterior
de compilação continua material, sem alegação de redução nesta entrega.
Rebuild incremental do núcleo, incluindo reconfiguração e link da lib,
após tocar apenas as fontes de cada grupo: fetch 9,841997 s e eventos
11,951341 s (uma amostra por grupo, sem mudar seu conteúdo). As TUs
comerciais não precisam ser regeneradas ou recompiladas por essas mudanças
de implementação. Não houve ganho de particionamento/incrementalidade novo.

## Equivalência e testes

As duas variantes isoladas, baseline, combinação, interpretação do baseline
e combinação com checkpoint real em 10 + 20 milhões produziram o mesmo
estado completo, RGBA e PCM em cada cenário:

| Cenário | Estado completo SHA-256 | RGBA | PCM, incluindo núcleo/ciclo |
|---|---|---|---|
| Janela canônica | `55befbcca037c55b56c0d8d3d8b5492490aed0a0b542272c99810cfc9f321316` | Sete campos iguais | 9.764 amostras; `38a75946727151fc1c0b7bc2256f9bc6ba8c516a0ce540d9d04f1465b0ce1b97` |
| Síntese ativa | `95513418e5bfaf360cb6d562bcfc84ba7d7ac5d3f04283211abedac0ef3dcf91` | Seis campos iguais | 9.766 amostras; `99e9fb7a54e76fc2051d40035d36b2f3ee750e75838d717f64ab2486fd850b3c` |

A janela curta conservou o hash anterior
`3775668692b1bb6263333eae3b4f529d4354ed1ac829e5eb51ab42a646ae1bcc`.
O estado serializado cobre EE, IOP, RAM, caches, conhecimento numérico,
DMA, IRQ, GS, SPU2 e relógios. Não se compara somente framebuffer/RAM.

No cenário ativo, uma amostra por variante foi usada para equivalência:
10,3759 s baseline, 9,96313 s fetch, 8,86185 s eventos e 8,86810 s
combinação. **Não houve benefício adicional do fetch sobre os eventos
nessa amostra.** Não é série estatística de desempenho nem ganho do jogo
inteiro; as medianas publicadas pertencem às cinco rodadas canônicas.

20 testes focados passaram, em 30,72 s: AOT/operandos, boot sintético,
cache, contexto/TLB, timers/IRQ, IOP/VBLANK, VU micro/DIV/WAITQ/PATH1,
VIF micro/MPG/UNPACK/DMA, GIF, scratchpad DMA, SIF0/SIF1, síntese SPU2 e
checkpoint completo. Regressões novas cobrem fetch de ROM/aliases,
alinhamento, bits desconhecidos, recusa MMIO, dirty D-cache versus
writeback e polling repetido antes da conclusão real de FLUSHE.

Evidência local: `out/aot-struct-summary.json` (40 medições),
`out/aot-struct-validation.json` (12 execuções de equivalência),
`out/aot-struct-provenance.json`, logs `out/aot-struct-*`, auxiliares de
medição e o log de recompilação integral. Nada comercial foi comitado.

CI de `ac7e405fc1483f0957cb5dd79f62358784c8fcb2`:
[núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42105484315)
e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/42105484103)
concluídos com sucesso em Windows x64/ARM64, Linux x64/ARM64 e macOS
Intel/Apple Silicon. Esses builds/testes não medem desempenho comercial
nesses outros hosts. A documentação não exige repetir a matriz do código
inalterado. Todos os ensaios locais foram encerrados.

## Limites

O cenário conserva as hipóteses experimentais anteriores. Isto mede trechos
do diagnóstico comercial, não boot original concluído, jogo inteiro, nova
compatibilidade ou tempo real. Não altera a BIOS original nem a fronteira
AA1 do trabalho de compatibilidade. Essas duas primeiras otimizações não
alteraram o codegen; a continuação abaixo mede o particionamento.

O perfil instrumentado posterior dos componentes não foi obtido: um build
intermediário recusou o esquema de checkpoint, antes de publicar estado.
Não se forçou a identidade nem se usou essa execução como perfil válido.
A API offline final fica em `aot_sources.hpp`, excluída do esquema de fio;
nenhum campo serializado foi alterado. A continuação revalidou os estados
completos anteriores, sem dispensar identidade, migração ou opt-in de runtime.

## Particionamento AOT — `3c3a5b5`

Referência adicional obrigatória: ReXGlue SDK
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`, BSD-3-Clause, consultado
somente para organização. Seu [emissor](https://github.com/rexglue/rexglue-sdk/blob/c94f5ebdcb3c9d1a460ca48e04f9758448f8d518/src/codegen/codegen_writer.cpp)
separa corpos, declarações necessárias e mapeamento, com
[partições estáveis](https://github.com/rexglue/rexglue-sdk/blob/c94f5ebdcb3c9d1a460ca48e04f9758448f8d518/src/codegen/output_partition.cpp)
e fingerprints de entrada. Usa [labels/chamadas diretas](https://github.com/rexglue/rexglue-sdk/blob/c94f5ebdcb3c9d1a460ca48e04f9758448f8d518/src/codegen/builders/control_flow.cpp),
contexto e base de memória explícitos, helpers de memória por largura e
seleção de MMIO, além de SIMDe no emissor vetorial e PCH no build.
PS2Recomp também possui corpos por função, registradores constantes,
acessos escalares especializados e eventos/deadlines no scheduler.
Seus serviços de kernel e modelos de memória não autorizam dispensar
TLB/cache, interrupções ou domínios numéricos neste runtime de sistema.
Não foi copiado, adaptado ou vinculado código de qualquer referência;
não se introduziu LTO obrigatório, SIMD obrigatório ou fast-math.

Implementação própria: cada bloco mantém **um** corpo, agrupado pelo endereço
inicial em faixas estáveis de 64 KiB, sem rebalancear funções preexistentes.
O nome da unidade é seu SHA-256; a tabela de PCs e a proveniência ficam
no dispatcher. Declarações cruzadas permitem os delay slots já encadeados.
O manifesto de dados confere todos os hashes, não executa CMake nem inclui
arquivos por glob. A CLI oferece `--split`; `ps2n-dev` usa esse caminho e
preserva os objetos de unidades idênticas, no cache exclusivo do perfil.
Corpos de instruções, guardas por instrução, fallback e hardware são os mesmos.

Na exploração, retirar a guarda inline reduziu a imagem local, mas não
demonstrou ganho de execução e foi descartado. Partições de 16 KiB ficaram
atrás das de 64 KiB nos três controles diretos com afinidade igual.
A primeira série sem afinidade teve dispersão alta; seus 30 resultados
foram preservados, mas não são o denominador dos ganhos abaixo.

Cinco rodadas finais alternadas por variante/janela, mesmo checkpoint,
entradas, compilador e flags anteriores, sem compilação concorrente.
Todas as variantes tiveram afinidade no processador lógico 14 e prioridade
AboveNormal, aplicadas antes da janela; nenhuma lógica convidada depende
disso. Medianas das cinco execuções, sem selecionar a melhor amostra:

| Variante | 1 milhão: s / milhões instr./s | 30 milhões: s / milhões instr./s | Campo real mediano |
|---|---|---|---|
| Referência anterior ao fetch/eventos | 0,355739 / 2,811 | 10,372600 / 2,892 | 1,696340 s |
| Fetch/eventos, TU única | 0,315632 / 3,168 | 9,167730 / 3,272 | 1,498635 s |
| Fetch/eventos + partições de 64 KiB | **0,298588 / 3,349** | **8,699110 / 3,449** | **1,422985 s** |

Ganho de throughput total: **19,14% / 19,24%**; incremento do particionamento
sobre a TU única: **5,71% / 5,39%**. A janela longa final variou de
8,695 a 8,723 s; as amostras exatas estão no JSON local. Não comparar esses
tempos diretamente com a série anterior, que não fixou afinidade.
Cobertura preservada: 990.868/1 milhão e 25.793.960/30 milhões AOT
(99,0868% / 85,979867%). Permanecem 9.132/4.206.040 instruções no
interpretador explícito: mesma cobertura estática e fronteiras de sistema,
FPU ou código não traduzido; nenhuma causa foi ocultada pelo particionamento.

As 30 execuções finais mantiveram estado completo, RGBA e ciclos dos campos.
Oito validações adicionais, em duas janelas, compararam TU única, partições,
interpretação pura e retomada de 10 + 20 milhões. Estado, RGBA, PCM e seus
ciclos coincidiram entre si e com a referência congelada anterior: 9.764
amostras na janela padrão e 9.766 com síntese ativa. O hash de estado inclui
CPU, RAM/cache/proveniência, EE/IOP/VU/GS/SPU2, ciclos e interrupções.
Os testes sintéticos também conferem escrita executável, interrupção em
delay slot, orçamento, retomada interior e reutilização real de objetos Ninja.

Executáveis comparados: TU única 11.772.928 bytes; partições 11.772.416
bytes, ambos **11,23 MiB**. O particionamento contém o custo de recompilação,
mas não reverte o crescimento histórico de 4,60 para 11,23 MiB.
Três pares de builds completos controlados (processadores lógicos 8/10/12/14,
prioridade igual, mesmas otimizações e três entradas lógicas: módulo,
idle e auxiliar, mais link): medianas de **124,015 → 131,721 s**;
**não houve ganho completo** (+6,21% de custo). Não usar os
89,09 s históricos como denominador de um build com outra afinidade.
Uma unidade de tamanho mediano superior, 930.662 bytes de C++ entre oito
partições, foi recompilada em **26,867 s**, incluindo reconfiguração e link.
O controle incremental monolítico invalidou somente o objeto do módulo,
reutilizando idle/auxiliar: **125,877 s**, com fonte original intacta.
A redução incremental foi **78,66%**. O ensaio particionado alterou somente
um comentário da unidade gerada, recalculou seu hash e restaurou o manifesto;
o teste sintético separado altera uma instrução de efeitos equivalentes.
Quinze builds sem alterações: mediana de **3,378 s**, incluindo inicialização
do ambiente MSVC. Cabeçalhos semânticos alterados continuam exigindo rebuild
das unidades dependentes. Não se afirma que particionar reduz sempre o build
completo ou desfaz o custo histórico da especialização.

A seção `.text` das duas variantes recompiladas integralmente coincidiu
com a dos executáveis usados na medição, por SHA-256. A recompilação de
uma unidade e o retorno ao objeto original também mantiveram `.text` idêntico.
Quatro testes locais pertinentes passaram: AOT smoke/ops, particionamento
nativo e pipeline gerenciado com corrupção/reparo de unidades. A sandbox
recusou a junção do último teste; a repetição com filesystem nativo passou.
CI final: [núcleo `3bf35c9`](https://github.com/fabegalo/ps2-native/actions/runs/42112647260)
e [launcher `3c3a5b5`](https://github.com/fabegalo/ps2-native/actions/runs/42110207761)
aprovados nos seis alvos Windows/Linux/macOS, x64 e ARM64. `3bf35c9`
move o teste Ninja para o passo com ambiente MSVC inicializado, corrigindo
a ausência de `kernel32.lib` no primeiro ensaio Windows; não altera runtime.

Evidência local: `out/aot-partition-affinity-final-results.json`,
`out/aot-partition-summary.json`, `out/aot-partition-validation.json`,
`out/aot-partition-provenance.json`, `out/aot-partition-controlled-builds*.json`,
`out/aot-partition-incremental.json`, `out/aot-partition-monolithic-incremental.json`
e logs dos mesmos prefixos. Os builds de medição ficaram congelados por
identidade de fontes/cabeçalhos/runtime para não reutilizar silenciosamente
uma referência após alterações. Conteúdo
comercial, estados, imagens, PCM e código traduzido permanecem ignorados.
Permanecem os custos da tradução/cache de dados e do fallback explícito;
não há perfil posterior válido que quantifique sua participação atual.
