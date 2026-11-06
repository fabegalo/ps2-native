# GS — residência e coerência incremental da VRAM

## Revisão e condições

Implementação própria BSD-3-Clause em `da485fc`, refinada em `580d381` e
`4fdd7dd`. A revisão final validada é **`4fdd7dde7e93abf7ed5ee6cf6f3f5e61d1512caf`**;
incrementos paralelos posteriores não participam destes números. A correção
de seleção D3D12 `e9bbc21` é trabalho paralelo já incorporado nessa revisão.
Não houve alteração de AOT R5900/VU1, firmware, clocks ou síntese SPU2.

Windows x64, MSVC 19.44 Release, AMD Ryzen 7 5700X3D e NVIDIA GeForce RTX
4070. Afinidade `16384`, prioridade AboveNormal, mesmas entradas, flags,
checkpoints e identidades verificadas. Os executáveis de referência e finais
foram congelados para evitar mistura com fontes/builds paralelos. As rodadas
de desempenho usam aquecimento igual e pelo menos cinco amostras alternadas;
compilação concorrente invalida a amostra. Hashes completos, RGBA e PCM são
validados separadamente, fora das janelas usadas para medir desempenho.

## Um estado lógico, duas localizações de armazenamento

As 512 páginas de 8 KiB registram se os dados recentes estão no host, no
buffer GPU ou em ambos. Essa localização é independente dos valores e das
máscaras de bits conhecidos: uma página nunca observada continua
desconhecida. Uploads iniciais incluem valores e máscaras das páginas
necessárias, mesmo quando nenhum bit foi escrito pelo convidado.

Lotes de até 256 primitivas reutilizam os buffers residentes. As páginas de
destino incluem conservadoramente toda a caixa validada, incluindo os casos
de máscara, teste ou cobertura que possam evitar uma escrita. A prova de
conhecimento por página é um limite conservador, nunca uma inferência de que
uma escrita possível ocorreu. Quando a prova é insuficiente, os acessos CPU
coerentes conservam a recusa original de bits desconhecidos.

Uma leitura CPU sincroniza sua página. Uma escrita CPU parcial materializa
primeiro a página alterada pela GPU, preserva os demais valores e máscaras
de bits e só torna essa página mais recente no host. O lote seguinte envia
somente as páginas necessárias alteradas pela CPU. Backend desligado não
atualiza metadados de residência a cada pixel; religá-lo invalida as provas
do cache. A troca de proprietário de um backend compartilhado materializa
o proprietário anterior antes de reutilizar seu armazenamento.

Texturas, CLUT, transferências locais, AA1 e outros fallbacks continuam no
renderer CPU existente. FINISH/SIGNAL/LABEL e TEXFLUSH completam a fila sem
baixar pixels que não consomem. Scanout reúne as páginas visíveis numa só
operação e pode juntar o readback ao último lote pendente. Cópias, inspeção
completa e checkpoint materializam todo o estado lógico necessário; handles,
filas e metadados de host não são serializados. O esquema permanece
`c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6`;
a identidade de runtime muda e mantém o opt-in de compatibilidade existente.

As submissões continuam síncronas. Barreiras entre primitivas, cópias e
acessos de host preservam dependências; a conclusão usa as fences com prazo
existentes. Exceções de execução mantêm a primeira causa observada e impedem
reexecução de um lote parcialmente executado. Não há apresentação direta
por textura GPU nem novo backend, runtime ou profiler.

## Regressões independentes

Os 25 testes focados locais passaram após o refinamento final: Vulkan e
D3D12 físicos (**1.062 primitivas / 223 lotes em cada API**), GS/GIF, raster
CPU, filtros, triângulos, eventos/IRQ, checkpoint completo e sessão experimental
Qt. As regressões incluem lotes consecutivos
sem readback, acesso seletivo e a páginas não relacionadas, CT24/CT16/T8/T4,
máscaras parciais, joins condicionais, conhecimento de FBA/SCANMSK pendente,
transferências sobrepostas, textura/CLUT e AA1 CPU, FINISH/SIGNAL/TEXFLUSH,
troca/movimento/destruição de proprietário e desligar/religar o backend.

Comparam VRAM e máscaras completas, RGBA, revisões/escritas, pacotes GIF,
estado completo, ciclos/IRQ e continuação de save/load. Testes de intervalos
validam limites, sentinelas e operações vazias. A ROM própria verifica um
checkpoint com comandos ainda pendentes; a sessão Qt verifica entrada e PCM
pelo runtime compartilhado. Não foi realizado Computer Use nesta tarefa.

## Medições

### Cinco pares completos — revisão intermediária `580d381`

O benchmark existente executou 512 primitivas, dois lotes, aquecimento igual
e cinco pares alternados antes/depois por API/carga. VRAM, máscaras,
contadores e RGBA coincidiram em todas as rodadas. Zero fallback CPU.
As amostras foram supervisionadas a cada 500 ms, sem concorrência observada.

| API/carga | GPU + sincronização antes | Em `580d381` | Diferença | Controle CPU antes → `580d381` |
|---|---:|---:|---:|---:|
| Vulkan pequeno | 12,890 ms | 10,760 ms | −16,52% | 22,798 → 23,617 ms |
| Vulkan preenchimento | 67,803 ms | 69,315 ms | +2,23%; sem ganho demonstrado | 861,456 → 898,244 ms |
| D3D12 pequeno | 8,445 ms | 5,245 ms | −37,89% | 22,787 → 23,321 ms |
| D3D12 preenchimento | 9,225 ms | 5,972 ms | −35,26% | 862,590 → 904,707 ms |

No preenchimento Vulkan, os desvios amostrais foram 3,368 e 4,408 ms;
a diferença não sustenta alegação de ganho ou regressão isolada do shader.
O controle CPU regrediu aproximadamente 2–5% nessa revisão. Isso motivou
`4fdd7dd`, que elimina chamadas e metadados de sincronização no acesso sem GPU
e o array temporário do acesso de uma página. Os testes de correção passaram;
**o desempenho final de `4fdd7dd` ainda não está certificado por cinco pares
completos em todas as cargas**. A coleta final foi encerrada por solicitação
do mantenedor, com amostras incompletas e variação elevada no host compartilhado.

### Cópias, esperas e execução dos lotes

Nas cinco rodadas completas de `580d381`, cada janela medida passou de
**16 MiB de upload e 16 MiB de readback de VRAM** para **zero upload e
320 KiB de readback** após aquecimento: redução de **98,05% no readback**.
O primeiro uso ainda envia as páginas necessárias; não se declara esse custo
gratuito. No D3D12 antigo, o contador incluía também 512 bytes de padding dos
dois blocos de resultado; a comparação acima normaliza somente valores/máscaras.
Os contadores agora discriminam 12 bytes de resultado por lote, separadamente.

Houve uma sincronização de readback de VRAM e duas submissões/esperas por
janela. O primeiro incremento intermediário fazia três submissões D3D12;
o readback fundido eliminou essa submissão extra. O número de fences final
permanece igual ao caminho anterior; não se afirma execução assíncrona.

Tempos medianos em milissegundos, antes → `580d381`:

| API/carga | GPU | CPU upload | CPU preparação/submissão do backend | CPU espera | CPU readback |
|---|---:|---:|---:|---:|---:|
| Vulkan pequeno | 6,218 → 6,262 | 0,914 → 0,0002 | 0,630 → 0,616 | 6,488 → 6,546 | 0,966 → 0,017 |
| Vulkan preenchimento | 61,380 → 64,221 | 1,016 → 0,0004 | 0,704 → 0,704 | 61,665 → 64,484 | 1,094 → 0,033 |
| D3D12 pequeno | 0,778 → 0,778 | 0,955 → 0,003 | 0,554 → 0,551 | 2,334 → 1,022 | 0,920 → 0,017 |
| D3D12 preenchimento | 1,656 → 1,653 | 1,049 → 0,012 | 0,712 → 0,668 | 3,174 → 1,985 | 1,002 → 0,024 |

O setup GS anterior ao backend participa do total e não tem temporizador
exclusivo adicional. Não somar tempo GPU à espera CPU, que já inclui sua
conclusão. A redução D3D12 vem de tráfego/custo do host; o tempo do shader
ficou praticamente igual. Estes números não são FPS comerciais.

### Memória

O estado lógico usa 8 MiB de valores/máscaras. Na revisão medida, arrays de
localização, páginas pendentes e prova de conhecimento usam 3.584 bytes;
os nove contadores de motivos acrescentam 72 bytes, excluídos padding e
handles. A fila contém no máximo 30 KiB de comandos. Os buffers alocados
reportaram 8.388.624 bytes em Vulkan e 25.427.968 bytes em D3D12, além dos
demais recursos do driver. Vulkan conserva memória visível/coerente do host;
residência não significa que se passou a exigir um heap device-local.
Esses valores não representam o working set completo do processo.

## Super Bust-A-Move

Os checkpoints locais do launcher foram retomados com os mesmos firmware,
NVM, disco, hipóteses revisadas, entradas soltas e módulos AOT verificados.
Não se repetiu o boot nem foram alterados os manifestos/checkpoints originais.

As validações de CPU e dos dois backends em `580d381`, nas janelas de
30 milhões de instruções, preservaram estado completo, VRAM/máscaras,
RGBA, PCM, ciclos EE/IOP e contagens AOT. A revisão final `4fdd7dd` também
reproduziu os estados CPU originais em ambas as janelas com o auxiliar antigo
recompilado integralmente e seus próprios cabeçalhos/biblioteca. No menu,
Vulkan e D3D12 da revisão final também preservaram o estado completo, os seis
campos RGBA, 9.766 amostras PCM e os 12 polls de entrada originais.
CPU e Vulkan também conservaram esses resultados na continuação comercial
com checkpoint em 10 + 20 milhões. O roundtrip comercial D3D12 ficou fora
do limite final de dois minutos; seu contrato sintético passou nos testes.

| Janela verificada | Campos originais | PCM estéreo capturado | Polls reais de entrada |
|---|---:|---:|---:|
| Menu atual | 6 | 9.766 | 12, botões `FFFF` |
| Checkpoint histórico | 7 | 9.764 | 14, botões `FFFF` |

Os fingerprints dos polls, bytes PCM, RGBA e estado completo coincidiram.
**Nenhuma primitiva comercial foi acelerada nessas janelas na revisão
avaliada**: menu, 282 fallbacks de textura e 12 de profundidade; janela
histórica, 28 de textura e 7 de profundidade. A categoria profundidade cobre
formatos/triângulos com Z fora do subconjunto GPU; não houve AA1 nesse trecho.
Não atribuir diferenças de throughput à GPU nem declarar gameplay novo.
Incrementos paralelos de cobertura posteriores ficam fora desta comparação.

O launcher Qt foi revalidado pelo teste de sua sessão real com fixture própria
(thread, entrada, scanout, PCM, pausa/retomada e checkpoint). A execução
comercial desta tarefa usou o auxiliar existente do runtime compartilhado e
o checkpoint/manifesto do launcher; não houve nova sessão comercial manual
na janela. **Não foi concluída a campanha final de cinco rodadas integradas
com tempo por campo e instruções/s**, portanto não há ganho comercial medido.
Tempos de validação com concorrência não são usados como desempenho.

## CI e limites da entrega

Snapshot de `4fdd7dd` em 05/11/2026, horário de São Paulo:

| Alvo | Núcleo | Launcher |
|---|---|---|
| Windows x64 | Aprovado | Aprovado |
| Windows ARM64 | Aprovado | Aprovado |
| Linux x64 | Aprovado | Em andamento |
| Linux ARM64 | Aprovado | Aprovado |
| macOS Intel | Aprovado | Aprovado |
| macOS ARM64 | Aprovado | Aprovado |

[Núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42134032747) e
[launcher](https://github.com/fabegalo/ps2-native/actions/runs/42134032778).
O núcleo passou nos seis alvos; o launcher passou em cinco e tem Linux x64
pendente. Não declarar ambas as matrizes inteiramente aprovadas.
Linux x64/ARM64 executaram Vulkan com Mesa 25.2.8 por software,
com GPU exigida e teste aprovado; DirectX foi pulado. macOS ARM64 pulou ambos.
Os testes físicos desta entrega são os locais na RTX 4070. A CI não certifica
GPU física nos demais hosts. Windows x64 executou D3D12 com sucesso e pulou
Vulkan por ausência de headers; a identidade física/virtual do adaptador não
está no log. Windows ARM64 e ambos os macOS pularam os testes GPU. O motivo
textual final do skip D3D12 ARM64 não está no CTest e não é inferido.

Permanecem necessários o fechamento da CI do launcher, cinco pares finais de todas as
cargas e a campanha integrada sem concorrência. Apresentação direta GPU,
ampliação de texturas/AA1 e comprovação de gameplay não pertencem a este
incremento. A limitação de cobertura comercial foi registrada, sem números
artificiais ou retirada de recursos para melhorar o resultado.

## Artefatos locais

- `out/gs-residency-intermediate-da485fc/` e
  `out/gs-residency-intermediate-580d381/`: amostras completas, CSVs, estados
  comparados e identidades das revisões intermediárias.
- `out/gs-residency-final-4fdd7dd/`: executáveis congelados e proveniência.
- `out/gs-residency-synthetic-4fdd7dd-results.json`: coleta final incompleta;
  não substituir a amostragem intermediária por uma razão de ganho final.
- `out/gs-residency-final-cpu-validation-results.json` e logs de validação:
  equivalência comercial; seus tempos concorrentes são ignorados.
- `out/gs-residency-core-build.log`: build e 25 testes focados da fotografia
  exata, incluindo Vulkan e D3D12 físicos.
- `out/gs-residency-completion-evidence.json`: resumo das validações finais,
  cobertura comercial, amostragem incompleta e auditoria de processos.

As fontes arquiteturais e a documentação primária de barreiras/fences estão
na [auditoria de referências](GS-GPU-REFERENCE-AUDIT.md). Nenhum código de
PS2Recomp/ReXGlue, shader de referência ou conteúdo comercial foi incorporado
ao repositório. Os artefatos locais com conteúdo do usuário permanecem em
`out/`, ignorados pelo Git.
