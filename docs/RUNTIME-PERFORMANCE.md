# Runtime: fronteiras de evento e tradução de memória — 05/11/2026

## Artefatos comerciais ausentes; incremento de preparação — 07/11/2026

Nesta execução o mantenedor confirmou ter apagado `out/`. Os discos e a
coleção local de firmware existem, mas os checkpoints do tabuleiro, manifestos
experimentais, executáveis pareados e perfis citados nas seções históricas não
existem neste checkout. Não há baseline comercial reproduzido nem sampling
Release novo; não foi escolhida uma otimização do núcleo a partir de custos
históricos. Sem sessão gráfica, push, PR ou CI remota.

O incremento independente implementado é a preparação observada do AOT e o
primeiro subconjunto executável de ps2autotests. No fluxo CMake/ps2n-dev real,
um callback carregado de dados passa de 5/3 instruções AOT/interpretadas para
8/0, com sequência de eventos e estado inspecionado iguais. O replay conserva
o SHA-256 do executável e reutiliza as partições. Isso valida descoberta,
preparação e equivalência dessa fixture; não mede FPS, áudio ou gameplay.
Detalhes, identidade e comandos em [DEV-TOOLS](DEV-TOOLS.md).

Não existem novos pares temporais comerciais nem dispersão a reportar. Os
tempos de build/execução dos testes incluem diagnóstico e não substituem a
janela primária. O lote ALU confere 120 resultados independentes em ambos os
motores; não promove compatibilidade dos títulos. Super Bust-A-Move conserva
apenas a evidência comercial histórica já descrita; Simpsons permanece sem
vídeo jogável demonstrado, e Shadow sem cena controlável certificada. Seus
checkpoints apagados precisam ser reconstruídos antes da próxima continuação.

Verificação adicional de checkpoint completo falhou por esquema na integração
e no controle sem este incremento, com o mesmo hash. Falha preexistente no
conjunto local preservado; não foi admitida migração presumida. Detalhes em
[DEV-TOOLS](DEV-TOOLS.md).

## Histórico — eventos/TLB de 05/11/2026

Código: `c23188e`. Implementação própria BSD; sem alteração de firmware,
aritmética convidada, clocks, renderer ou código proprietário distribuído.

## Gargalo e referência

O perfil anterior do menu registrou 65.205.240 chamadas de sincronização,
IOP e dispositivos em 30 milhões de instruções EE. GS representava cerca
de 1% do tempo exclusivo instrumentado. Esses tempos incluem overhead de
profiling e não são parcelas dos tempos Release abaixo.

O [scheduler do PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp)
foi referência de organização por próximos eventos, sem copiar código ou
transportar seu kernel HLE. O runtime mantém o firmware convidado e a execução do IOP.
A busca linear nas 48 entradas TLB também continuava em cada acesso traduzido.

## Mudanças e contratos

- A sincronização consulta o estado atual antes de entrar nos motores.
  Conserva a fronteira IOP 8:1, mudanças/desconexão GS mesmo sem aposentadoria,
  Q0/Q1 após E, VIF pendente, recusas e timestamps gráficos de checkpoint.
  DMA ativo continua no parser original, inclusive antes do próximo beat,
  para preservar prioridade e recusa imediatas. Não há deadline persistente
  que possa sobreviver a uma escrita de registrador sem invalidação.
- O cache TLB por instância guarda somente o índice de uma correspondência
  única, por página virtual de 4 KiB e ASID. Toda entrada compara apenas
  bits acima dessa página; o caminho conserva seleção da metade, S, V/D,
  modo de cache e endereço físico. Privilégio e alinhamento continuam no
  barramento comum. Bytes, conhecimento e falhas não são armazenados no cache.
- Escritas TLB e reinicializações invalidam o cache. A leitura de checkpoint
  também limpa os índices copiados do destino antes de substituir a TLB.
  Campos serializados e esquema `c6495503…` permanecem idênticos; os helpers
  e metadados host novos participam integralmente da identidade de runtime.
  A troca de runtime continua exigindo opt-in explícito.

## Medição comercial

Windows x64, Ryzen 7 5700X3D, MSVC 19.44, Release, profiling desativado,
mesmos módulos AOT, entrada solta, afinidade 16384 e prioridade AboveNormal.
Retomada do manifesto local `interactive-menu.json`, orçamento de 30 milhões,
CPU GS para isolar o runtime. Carga e hashes finais ficam fora do intervalo;
scanout dos campos originais fica dentro. Nenhuma compilação concorrente
da tarefa. Os executáveis de controle e sincronização isolada foram congelados.

Cinco trios alternados, mais um trio de aquecimento excluído previamente.
Os 25 intervalos por variante correspondem a cinco intervalos de cada execução.

| Variante | Mediana: 30 milhões (s) | CPU total do processo (s) | Campo original (s) |
|---|---:|---:|---:|
| Controle contemporâneo | 11,341572 | 12,640625 | 1,838974 |
| Sincronização isolada | 10,722638 | 12,031250 | 1,733293 |
| Sincronização + cache TLB | **7,776684** | **9,078125** | **1,254745** |

Combinação: throughput mediano +45,84%, tempo de execução −31,43%.
O tempo de CPU do processo, que inclui carga/relatório/hashes, caiu 28,18%.
Faixas de execução: controle 10,182395–14,494990 s; sincronização
9,260945–10,928277 s; combinação 7,751996–7,936340 s. A combinação venceu
os cinco pares contra ambos os controles. Houve interferência externa,
inclusive no aquecimento; todos os valores foram conservados. O percentual
não é promessa de ganho universal nem medição de uma máquina ociosa.

Uma coleta independente anterior da sincronização isolada teve mediana
11,537564 → 10,936279 s (+5,50% de throughput). No trio final houve um par
em que ela perdeu para o controle. O ganho maior vem da combinação com TLB;
não atribuir à sincronização sozinha o resultado total.

Executável de medida: 11.940.352 → 11.949.056 bytes (+8.704).
Cache host: 256 entradas de índice por Memory (2 KiB nesta ABI).
Launcher local `out/launcher-msvc/ps2-native.exe` recompilado com o mesmo núcleo
e AOT. Não houve comparação de tempo de build nem nova sessão visual.

## Equivalência e limites

Os 18 ensaios da matriz conservaram o estado completo
`80c2d0d3f6635d1b24934ca1853b006f592888bd7a9293daa4126c10cf4da9a5`,
25.446.236 instruções AOT e 4.553.764 interpretadas em cada janela.
Validações separadas conferiram seis campos RGBA, VRAM e máscaras completas,
9.766 amostras PCM com timestamps, 12 polls SIO2, estado e ciclos/IRQ:
controle/novo, interpretação pura, checkpoint de 10 + 20 milhões, Vulkan
e Direct3D 12. Originais de firmware, disco e checkpoint foram preservados.

Vinte testes focados passaram: boot/TLB, ASID, permissões, sobreposição,
páginas grandes, contexto/cache EE, AOT, timers/IRQ, IOP/GS, DMA/SIF,
VIF/VU/Q, SPU2, checkpoint e sessão experimental Qt. A primeira verificação
de checkpoint detectou a mudança indevida de identidade pelos helpers;
o esquema original foi restaurado sem afrouxar a validação.

**1,254745 s por campo continua muito acima de tempo real.** Não demonstra
primeira partida ou gameplay. Busca de instruções continua verificando bytes;
despacho/observação e amostragem por instrução permanecem, assim como o
interpretador explícito. Um novo perfil deve medir esses custos após TLB.

CI de `c23188e` ainda não executada: a revisão automática bloqueou o push
para `origin/main`, exigindo autorização específica para publicar as alterações.
Nenhum ensaio comercial permanece ativo ao encerrar. Evidência local em
`out/runtime-final-summary.json`, `runtime-final-performance-results.json`,
`runtime-final-*-results.json` e `runtime-tlb-tests.log`; não distribuir
artefatos comerciais ou seus traces.

## Busca de instruções por página — 07/11/2026

Pedido: desempenho e fidelidade de Super Bust-A-Move, por execução direta,
sem launcher. O profiler existente agora separa interrupções e observação;
os escopos desaparecem dos builds sem profiling. Antes da mudança, a coleta
registrou 30.000.006 consultas de interrupção, 30 milhões de observações e
35.093.967 acessos de memória. Tempos instrumentados não são parcelas do
tempo Release.

### Mudança e isolamento

`Memory::fetch32` reutiliza a resolução de até 16 páginas completas de 4 KiB
de RAM, ROM ou scratchpad. A chave inclui página virtual, ASID, privilégio,
ERL e mapeamento. Escritas TLB, reset/carregamento e leitura de checkpoint
invalidam o cache host. Alinhamento continua obrigatório; páginas parciais
e dispositivos usam a resolução comum. Bytes e máscaras de conhecimento
são lidos novamente após sincronização DMA. Não são armazenados conteúdo
executável, conhecimento ou falhas. A comparação dos bytes AOT permanece.

384 bytes host adicionais por Memory nesta ABI, sem campos novos no
checkpoint, configuração pública, dependências ou regra por título.
Fontes congeladas a partir de `7e7ceda` mais o trabalho local existente,
registradas em `out/perf-nov07-baseline-provenance.json`. O par muda somente
`src/memory.cpp` e o cache host de `memory.hpp`; trabalho concorrente foi
preservado. Núcleo e traduções AOT recompilados juntos.

MSVC 19.44, `/O2 /Ob2 /DNDEBUG`, profiling OFF, afinidade 16384, prioridade
AboveNormal, mesmo manifesto `interactive-menu.json`, checkpoint, firmware,
disco verificados e entrada FFFF. GS CPU para isolar execução. Carga e hashes
finais fora da janela; scanout original dentro. Nenhuma compilação da tarefa
durante esta matriz. Cinco pares alternados, após aquecimento excluído previamente:

| Par | Controle (s / 30 milhões) | Cache de resolução (s) |
|---|---:|---:|
| 1 | 12,1153910 | 10,7895352 |
| 2 | 8,1082155 | 7,8050025 |
| 3 | 7,9320712 | 7,3612893 |
| 4 | 7,9313101 | 7,3675274 |
| 5 | 7,9974762 | 7,5366949 |
| **Mediana** | **7,9974762** | **7,5366949** |

Throughput mediano **+6,11%**, tempo **−5,76%**. Todos os cinco pares
favoreceram a mudança; houve variação externa, sobretudo no primeiro.
Aquecimento: 8,1666972 / 8,5739689 s, sem favorecer o cache.
Mediana dos 25 intervalos: **1,256657 → 1,174710 s por campo original**.
CPU total do processo, incluindo carga/hashes: 9,609375 → 9,156250 s.
Executável de medida: 12.016.128 → 12.017.152 bytes. Não é tempo real nem
medição de updates do jogo.

Duas candidatas foram descartadas: divisores especializados dos timers
venceram dois de cinco pares; consulta especializada de conhecimento de
palavras alinhadas venceu um de cinco. Não entraram no núcleo. Todos os
valores, incluindo aquecimentos, ficaram em
`out/perf-nov07-{timers,memory,fetch}-performance-results.json`; resumo em
`out/perf-nov07-fetch-summary.json`. O aquecimento inicial dos timers
coincidiu com o teste de checkpoint e não foi usado como evidência de ganho.
Os casos de borda dos três prescalers ficaram no snapshot local da candidata.

### Equivalência e referências

Dez validações pareadas: CPU, interpretação pura, checkpoint 10 + 20 milhões,
Vulkan e D3D12. Todas conservaram estado completo
`82c384eb3fc5822d9ea3469d265c514ab904ca16e6c0c02441ca07e37194c2ed`,
VRAM/máscaras, seis campos RGBA 640 × 224, 9.766 amostras PCM com timestamps,
12 polls de entrada, ciclos EE/IOP e efeitos modelados. Na janela híbrida,
25.446.236 instruções AOT e 4.553.764 interpretadas; VU1 não executou.
RTX 4070: 294 primitivas reais por backend (282 texturizadas e 12 flat),
108 lotes, zero fallback. Seus tempos são de validação, não um benchmark
pareado dos backends. Evidência em `out/perf-nov07-validation-summary.json`.

Oito regressões focadas passaram no snapshot pareado: boot híbrido/TLB,
cache/contexto EE, timers/IRQ, HBLANK IOP, quadro/timer Direct ELF e checkpoint.
Novos casos cobrem máscaras após stores, isolamento do scratchpad,
privilégio/ERL e ROM1 parcial. Testes existentes cobrem ASID,
mudança de TLB, páginas grandes, alteração de bytes e restauração aquecida.

Uma segunda compilação, de `7e7ceda` mais apenas esta alteração, passou
sete dos oito testes. `native-ee-context` ainda espera a recusa de
`MADD.S` para `1*2+1,25`, já suportado pelo núcleo. A implementação anterior
de `fetch32`, recompilada nessa mesma cópia, falha exatamente na mesma
asserção (`multiplier rounding not modeled`). É falha preexistente da base;
o teste corrigido nas edições concorrentes passou no snapshot pareado.
Essa correção alheia não foi incluída neste incremento.
Após sua integração em `3a40856`, uma nova cópia isolada desse commit mais
esta alteração foi recompilada: **8/8 testes passaram**, incluindo contexto
EE e checkpoint completo. A identidade de esquema permaneceu `5662ca9c…`.
Logs: `out/perf-nov07-final-clean-{build,tests}.log`.

Perfil posterior do snapshot pareado, obtido separadamente e com o mesmo
estado final: EE não atribuído 22,53%, interrupções 17,95%, observação
13,46%, memória 11,44%, IOP 10,81%, verificação de código 9,83% e GS CPU
3,74% do tempo exclusivo instrumentado. A instrumentação faz dezenas de
milhões de leituras de relógio e altera essas proporções. Usar os caminhos
identificados para uma próxima coleta Release, não como previsão de ganho
nem justificativa para retirar validações. Dados em
`out/perf-nov07-profile-final-results.json`.

Referências consultadas, sem copiar ou integrar código externo:

- [Play!: comandos por quadro](https://github.com/jpd002/Play-/blob/master/Source/gs/GSH_Vulkan/GSH_VulkanFrameCommandBuffer.cpp)
  e [licença permissiva](https://github.com/jpd002/Play-/blob/master/License.txt): submissões/fences.
- [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs/blob/main/README.md):
  compute/replay e limites declarados de precisão física de interpolação/AA1.
- [PCSX2: texture cache](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSTextureCache.h)
  e [Counters.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Counters.cpp):
  validade/invalidação e organização por eventos, sob GPL-3.0+.
- [Play!: COP_SCU](https://github.com/jpd002/Play-/blob/master/Source/COP_SCU.cpp):
  comparação arquitetural; o incremento artificial de Count por leitura
  não foi transportado para este runtime.

O núcleo permanece original BSD. A autorização para usar referências GPL
não resultou em incorporação nesta etapa. Sem sessão humana, áudio ouvido,
launcher, primeira partida ou certificação de outros dispositivos. A melhoria
medida pertence ao menu headless. CI/publicação não executadas nesta etapa.
