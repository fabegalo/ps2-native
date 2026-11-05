# AOT nativo — 05/11/2026

Código: `4b113b0`, `4651b36` e `d0fdc39`. Arquitetura AOT preservada;
nenhuma alteração no launcher ou no trabalho paralelo de compatibilidade.

## Mudanças comprovadas

- Corrigido `native-m29-drawing`: o primeiro vértice STQ é enfileirado sem
  desenhar. O teste continua exigindo recusa ao consumir textura não escrita.
- Palavra MIPS constante no template, acessores de GPR compartilhados com
  `EEState`, inlining explícito e erros fora do caminho frequente. Índices,
  operandos, máscaras simbólicas, domínios finitos e metade alta continuam
  obedecendo à mesma semântica do interpretador.
- Encadeamento direto do desvio para seu delay slot; cache de despacho por
  execução e identidade do módulo. Cada instrução comprovada possui entrada
  de retomada, compartilhando o corpo original do bloco. São 6.217 funções e
  17.364 entradas no módulo comercial; não há funções de sufixo duplicadas.
- Cópias nativas portáveis de bytes RAM/cache e uma consulta de conhecimento
  por palavra, preservando tradução, privilégios, alinhamento, cache e ordem
  DMA/MMIO. Histórico diagnóstico circular sem alocações recorrentes.
- Propagação estática de constantes através de cópias e aritmética de GPR.
  Entradas que contornam a construção, valores carregados sem prova e LUI
  em delay slot continuam recusados. A fixture M2.2 agora reconhece a cópia
  exata `ADDU r,r,$zero`, mantendo seus três casos negativos.

## Medição comercial pareada

Windows x64, Ryzen 7 5700X3D, MSVC 19.44.35229, C++20,
`/MD /EHsc /O2 /bigobj /DNDEBUG /utf-8`, sem profiling. Referência congelada
antes das otimizações; mesmas alterações locais preexistentes de integração
host/IOP nos dois lados, sem callbacks de entrada conectados. Cinco amostras
por motor/janela, ordem alternada, sem compilação concorrente. Tempos incluem
execução e relatório comum; excluem carga, hash final e salvamento.

| Janela | Tempo mediano antes → depois | Instruções/s antes → depois | Ganho de throughput | AOT antes → depois |
|---|---|---|---|---|
| 1 milhão | 0,401065 → 0,356687 s | 2,493 → 2,804 milhões | **12,44%** | 99,0864 → 99,0868% |
| 30 milhões | 11,5088 → 10,5106 s | 2,607 → 2,854 milhões | **9,50%** | 85,979483 → 85,979867% |

Na janela longa, redução de tempo de 8,67%; intervalo das cinco amostras
11,4905–11,5542 s antes e 10,4595–10,6253 s depois. Os sete campos reais
coincidiram; mediana de 30 intervalos consecutivos: **1,881625 → 1,719560 s
por campo**. Isso não mede apresentação contínua em tempo real nem 60 FPS.

Outro checkpoint, com síntese ativa: 30 milhões em 11,3334 → 10,2905 s,
2,647 → 2,915 milhões de instruções/s e 98,402247 → 98,402670% AOT.
Uma amostra por motor nesse trecho, usada principalmente para equivalência.

Pico de working set do processo, incluindo carga: aproximadamente 276 MiB
nos dois motores na janela longa; 280 MiB no trecho com áudio. O executável
local cresceu de 4,60 para 11,23 MiB. Compilar as mesmas três TUs e ligar:
24,560 → 89,094 s (**3,63×**). Regeneração com saída idêntica, mediana de
cinco amostras intercaladas, ambos os geradores sem profiling:
0,134105 → 0,212107 s. Todas reutilizaram o arquivo, sem reescrevê-lo.
O custo de código e build é uma limitação material desta especialização.

## Custos por componente

Outra execução de 30 milhões, com profiling compilado e habilitado nos dois
motores. Tempos **exclusivos instrumentados**, não parcelas do benchmark
sem instrumentação; o profiler eleva o total para 30,082/29,710 s.

| Componente | Antes | Depois |
|---|---:|---:|
| Despacho | 0,436233 s | 0,248001 s |
| Verificações de código, excluindo memória/dispositivos internos | 1,743741 s | 1,766989 s |
| Memória | 4,873748 s | 4,834402 s |
| Sincronização | 6,551150 s | 6,509938 s |
| Dispositivos | 5,803447 s | 5,804252 s |

Passagens pelo dispatcher: 11.000.459 → 7.608.012 (**−30,84%**);
tempo instrumentado de despacho −43,15%. Chamadas de sincronização:
62.813.593 → 62.813.598. Não se atribui ganho a agrupamento temporal de
dispositivos: essa otimização não foi feita. As verificações de bytes e o
polling de IRQ continuam em cada instrução.

## Equivalência e validação

- Mesmo hash completo de EE, IOP, RAM, caches e dispositivos antes/depois e
  na interpretação de referência em ambas as janelas, no trecho com áudio
  e nas continuações partidas em 10 + 20 milhões com salvar/carregar real.
- Sete campos RGBA na janela longa e seis no trecho ativo: dimensões,
  ciclos e hashes iguais. Nenhum frame substituído ou sintetizado no host.
- PCM dos dois núcleos SPU2, incluindo núcleo e ciclo de cada amostra:
  9.764 amostras iguais no carregamento e 9.766 iguais no trecho ativo,
  também após retomada. DMA, IRQ, exceções e ciclos permanecem no estado
  completo comparado; os testes focados exercitam essas fronteiras.
- 27 testes locais passaram: execução AOT, raízes/tabelas/cópias, retomada
  dentro dos blocos sem fallback, escrita executável no mesmo bloco, IRQ em
  delay slot, MMIO/TLB/cache, DMA, VU, RGBA, síntese e checkpoints.
- CI do código `d0fdc39`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41999528499)
  e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/41999528308)
  aprovados nos seis alvos Windows/Linux/macOS, x64 e ARM64.

Evidência local preservada em `out/aot-opt-final2-results.json`,
`out/aot-opt-summary.json`, `out/aot-opt-profile-*-30m.log`,
`out/aot-opt-generation-results.json`, logs de build e testes correspondentes.
São auxiliares locais sobre o runtime existente; firmware, jogo, checkpoints,
traces e traduções comerciais não foram comitados.
Nenhum ensaio comercial permanece ativo.

## Limites

Os cenários experimentais preexistentes e os vínculos de BIOS/ROM1/NVM/disco
foram conservados. As medições ainda usam as raízes explícitas verificadas
anteriores (`0x100068`, `0x100084`, `0x1D2EF8`) e o módulo idle de RAM com
identidade conferida. Não demonstram descoberta automática equivalente sem
essas raízes, cobertura do jogo inteiro, boot original ou novo gameplay.

AOT/interpretador continuam compartilhando a semântica existente. Não se
removeu fallback para código não comprovado, não se agruparam ciclos/IRQs e
não se implementou invalidação por página: falta auditar e provar todas as
vias de escrita, inclusive DMA, cache/writeback e restauração. O ganho é
incremental e sustentado nestas janelas, ainda distante do tempo real.
