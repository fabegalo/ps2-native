# Referências técnicas versus requisitos do ps2-native — 03/11/2026

## Geometria DVD-9 e TOC consultadas — 07/11/2026

Consultados [ECMA-267, seção Physical Format Information](https://ecma-international.org/wp-content/uploads/ECMA-267_1st_edition_december_1997.pdf)
e [ps2sdk cdvdman](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/iop/cdvd/cdvdman/src/cdvdman.c)
para campos PFI, endereços físicos, track path e consumo convidado da TOC.
O leitor ISO local foi reutilizado. Play! Cdvdman é acoplado ao HLE; PCSX2
foi apenas comparação de layout, sem copiar/adaptar código GPL. A implementação
local usa o formato documentado e testes sintéticos independentes.

Uma ISO de setores de usuário não comprova a PFI física: a candidata exige
`DiagnosticDvdGeometry` explícita, valida os dois volumes e inclui o parâmetro
na identidade imutável dos checkpoints. PFI opaca continua desconhecida;
nenhuma geometria específica do God of War foi codificada no runtime.
A divisão inferida da imagem e OTP assumido no probe não são medição física.

## MAC/PHY e reset FIFO SMAP consultados — 07/11/2026

Referência primária fixada: ps2sdk `d41ea62946e53d9a0c8788bb9eb8c8faf488b559`,
[`smapregs.h`](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/common/include/smapregs.h),
[`speedregs.h`](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/common/include/speedregs.h)
e [`ps2dev9.c`](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/iop/dev9/dev9/src/ps2dev9.c).
O driver escreve RESET nas portas de byte TX/RX e aguarda sua limpeza. O bit de
capacidade SMAP é separado da revisão SPEED. Isso fundamenta o protocolo,
mas não mede sua latência física.

A mesma revisão fundamenta MODE0 high-halfword-first, BD_SWAP por halfword,
64 descritores de oito bytes por anel, FIFOs de 4 KiB TX/16 KiB RX, W1C,
BMCR no PHY de endereço um e o teste de loopback do driver. Foram reutilizados
esses contratos de registradores e o fluxo como evidência; a máquina de estados,
filas e CRC são implementação local, sem copiar o driver. O PHY DP83846A e
seus bits reset/loopback são identificados pelo próprio `smapregs.h`.
Prazos de MDIO, reset e pacote permanecem hipóteses explícitas. O vetor CRC32
`123456789` e um programa convidado sintético verificam os bytes recebidos.

`SmapFifos` é implementação original: estado desconhecido antes da escrita,
conclusão por prazo IOP explicitamente selecionado, FIFO vazia após reset,
invalidação em energia e serialização completa. Nenhum código AFL/GPL foi
copiado. Play! `Source/iop/Iop_Dev9.cpp` foi consultado, mas seus retornos zero
para acessos desconhecidos não constituem componente adequado a este contrato.
Resultados e limitações em [M2.8-STATUS](M2.8-STATUS.md).

## Reutilização efetiva do leitor IPU — 07/11/2026

**Código incorporado:** `bits_at` de Samir Nuri, [ps2-web-recomp,
e026b971324068773eed88bf502437de9e581b6b](https://github.com/snuri00/ps2-web-recomp/blob/e026b971324068773eed88bf502437de9e581b6b/runtime/src/ipu/ipu.cpp),
adaptado para a fila por instância em `include/ps2native/ipu.hpp`.
Atribuição e MIT preservados em `third_party/ps2-web-recomp/LICENSE`;
escopo em [LICENSE-DECISION](LICENSE-DECISION.md#reutilização-efetiva-leitor-do-ipu--07112026).

Antes, o runtime recusava `SETIQ` com FB diferente de zero e cargas de tabelas
fora da fronteira de qword. Agora lê bytes MSB-first no BP estabelecido por
`BCLR`, aplica FB de `SETIQ`, retém a qword parcial e só publica a tabela
quando todos os bits existem. Mantém ordem de entrada, limites da FIFO,
BUSY, IRQ e recusas de dados desconhecidos/opções reservadas. `SETVQ` também
usa o BP corrente. É o caminho real de MMIO/FIFO, sem comando alternativo.

Validação Release MSVC x64 (`/O2 /Ob2 /DNDEBUG /Z7`, Ninja, GPU e launcher
desativados), fontes congeladas em `out/reference-ipu/`:

- `native-ipu-tables`: passou; 8.192 pares BP/FB, 128 posições SETVQ,
  sequência de bytes independente atravessando qwords, substituição pendente,
  reset, FIFO e recusas. O mesmo teste na implementação anterior falha na
  recusa de alinhamento, antes da alteração.
- `bios-gs-ipu-control`: passou; escritas de CPU no FIFO só encerram BUSY e
  geram a interrupção após a última qword necessária.
- `native-complete-checkpoint`: passou; inclui carga parcial BP=127/FB=63,
  restauração em memória nova, conclusão e igualdade de estado completo.
  Migração opt-in aceita somente fingerprints auditados com codec inalterado
  e reserialização idêntica; schemas arbitrários continuam recusados.
- Tabelas e MMIO passaram também em `head-source`, HEAD mais somente os
  hunks desta entrega. O teste completo de checkpoint usa o snapshot local
  que inclui correções concorrentes, entre elas a construção de RDRAM já
  pendente; essas alterações não foram incorporadas neste commit.

As tabelas são conservadas no formato já usado pelo ps2-native. Não foram
importados a ordenação zigzag interna, o decoder MPEG completo, HLE ou
temporização do doador. Nenhum jogo, vídeo completo, sessão do launcher ou
ganho de FPS foi validado nesta etapa; não se declara um título novo compatível.

> Observações baseadas no README e em arquivos específicos dos repositórios consultados. Não são prova de que todo código desses projetos funciona em jogos reais. As auditorias anteriores registram consultas sem integração; a incorporação efetiva do leitor IPU está documentada acima.

## Estudo para acelerar desenvolvimento, compatibilidade e execução — 07/11/2026

### Conclusão e escopo

O maior retorno provável é reduzir o trabalho repetido no caminho nativo e
reutilizar testes e componentes pequenos de outros projetos. Aumentar somente
a porcentagem AOT, começar outro renderer ou abrir muitos jogos simultaneamente
não resolve a distância atual até tempo real. Esta é uma conclusão de engenharia
com base nas evidências abaixo; o próximo gargalo exclusivo ainda exige sampling.

Estudo do checkout em `main`, HEAD `9b1a20f`, com alterações concorrentes já
presentes em CPU/VU/GS/checkpoint e relatórios. Essas alterações foram preservadas.
Foram examinados os fluxos de geração, execução, memória, IOP, checkpoint,
preparação e sessão experimental, os relatórios atuais e referências externas.
Não foi uma auditoria linha a linha de todo o núcleo. Ponytail `full` aplicado
a partir da skill local; skills externas abaixo foram estudadas, sem instalação.
Nenhum jogo, sessão gráfica, instalação ou benchmark novo foi iniciado.

### O que os números locais estabelecem

As séries existentes foram relidas e as medianas recalculadas nesta investigação.
Não somar ganhos de séries com executáveis, hashes de estado ou cenas diferentes.

| Experiência local | Antes → depois | Interpretação |
|---|---|---|
| Entradas EE observadas, tabuleiro, cinco pares | 1,5625226 → 1,5051987 s / 30 milhões; cinco vitórias | +3,81% de throughput; tempo −3,67%; AOT 97,2480% → 99,4917% |
| Cache de identidade 1024 → 4096, cinco pares | 1,6133960 → 1,5737666 s | Melhora pequena e consistente nessa série, já contemplada no código inspecionado |
| Experiência anterior de observação, três pares | 1,6274629 → 1,7525385 s | Regressão de 7,69% no tempo mediano; preservar como resultado negativo |
| Partições AOT, relatório estrutural anterior | incremental 125,877 → 26,867 s; completo 124,015 → 131,721 s | Reuso incremental demonstrado; build completo ficou mais caro |

Fontes locais: `out/realtime-observed-aot-{primary,validate}-results.json`,
`out/realtime-gameplay-observed-evidence.json`,
`out/realtime-code-capacity-primary-results.json`,
`out/realtime-observer-primary-results.json` e
[AOT-STRUCTURAL-PERFORMANCE](AOT-STRUCTURAL-PERFORMANCE.md#particionamento-aot--3c3a5b5).
Na validação separada das entradas observadas, conferiram-se os seis campos
RGBA, PCM, entrada, estado GS e hash completo: iguais no par do mesmo formato
`550e7840…`. O artefato de geração registra zero palavras divergentes do ELF,
1.006 entradas adicionais e 93.437 instruções estáticas. Sua aceitação escrita
ainda dizia pendente; a conferência dos JSONs não certifica integração na HEAD.

Esses seis campos representam aproximadamente 0,1 s convidado: 1,5052 s de host
ainda significa cerca de 15 vezes abaixo da velocidade original. Cobertura AOT
conta instruções, não participação no tempo host. Eliminar 10% do tempo total
permite no máximo 1,11× de aceleração; chegar a 15× exige eliminar cerca de
93,3% do custo atual para a mesma progressão. É uma conta de escala, não uma
previsão de ganho. Cinco intervalos entre seis campos não permitem estimar p99.

Os caches inspecionados usam MSVC Release `/O2 /Ob2 /DNDEBUG`, profiling OFF.
O build do teste aponta para `out/realtime-cpu-review-source`; o launcher aponta
para o checkout principal e ainda configura traduções em
`out/aot-partition64-code/optimized.cpp` e `out/aot-opt-build/optimized-idle.cpp`.
Portanto, o ganho do executável headless não demonstra melhora da janela do
mantenedor. O cache CMake não prova quais bytes estão no executável instalado.

### Por que traduzir mais não basta

- `src/aot.cpp::emit_block` emite `aot_enter_checked`, `ee::begin`, a semântica
  compartilhada e `ee::retire` para cada instrução. O gerador já chama diretamente
  alguns delay slots; encadear blocos/chamadas é uma extensão, não partir do zero.
- `include/ps2native/ee_semantics.hpp::aot_enter_checked` ainda verifica PC,
  orçamento, interrupções, identidade do código e observação. O runtime já tem
  janelas de IRQ e agregação de esperas provadas. Não repetir o experimento de
  agrupar aritmética que ficou mais lento em `RUNTIME-PERFORMANCE`.
- `src/memory.cpp::code_matches` consulta versões por página e geração DMA;
  pode sincronizar IOP antes do hit. Reutilizar uma prova exige invalidá-la por
  stores, cache/writeback, TLB, DMA, observadores e restauração, quando aplicável.
- `src/boot.cpp::Tracer` permanece ligado durante a execução. O trace cheio já
  evita parte da formatação, mas ainda há contabilidade por instrução. A hipótese
  de outro observador mais barato precisa de medida: uma tentativa anterior perdeu.
- `src/iop.cpp::run_until` já tem `synchronization_due` e `advance_idle_loop`,
  porém também executa R3000A e bombeia dispositivos por ciclo. Avançar por eventos
  ou especializar trechos conhecidos são candidatas se o perfil atual apontar IOP.
- As máscaras de conhecimento e provas numéricas participam da correção e dos
  checkpoints. Representação compacta/caminho de valores conhecidos pode reduzir
  custo; apagar essa informação altera o contrato e não é uma otimização equivalente.

O perfil instrumentado histórico ajuda a localizar chamadas, mas lê o relógio
milhões de vezes. Suas porcentagens não quantificam os gargalos da variante de
99,49% AOT. Usar sampling do Release com símbolos correspondentes, em execução
separada da medição primária; a [documentação Microsoft](https://learn.microsoft.com/en-us/visualstudio/profiling/cpu-usage?view=visualstudio)
descreve a ferramenta existente. Não criar outro profiler para iniciar esta etapa.

### Projetos pesquisados e reaproveitamento concreto

Consulta em 07/11/2026. Links `main`/`master` são móveis; fixar o commit e o
conteúdo dos arquivos no incremento que realmente os incorporar. Afirmações
de gameplay/desempenho dos doadores não foram reproduzidas aqui. Priorizar
adaptações que mantenham o núcleo comum, sua identidade e seus testes.

| Referência primária examinada | O que aproveitar | Integração e limite técnico |
|---|---|---|
| [N64Recomp, `cgenerator.cpp`](https://github.com/N64Recomp/N64Recomp/blob/main/src/cgenerator.cpp) e [overlays](https://github.com/N64Recomp/N64Recomp/blob/main/README.md#overlays) | Chamadas estáticas diretas; lookup para indiretas e imagens carregadas | Adaptar em `aot.cpp`/dispatcher. Manter saída por budget, IRQ, exceção, stop e retorno não convencional; os registradores locais do N64 não provam equivalência R5900 |
| [gow-native-pc, patch 28](https://github.com/Vnnc1/gow-native-pc/blob/main/engine/patches/28-recomp-direct-jal.patch) e [patch 30](https://github.com/Vnnc1/gow-native-pc/blob/main/engine/patches/30-mem-fast-path.patch) | Chamada leve com referência completa; classificação rápida de RAM e casos de borda | Reusar testes e adaptar o algoritmo. A máscara de espelhos não substitui TLB, permissões, cache ou conhecimento do ps2-native |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/main/README.md) e [`ps2xIOP`](https://github.com/ran-j/PS2Recomp/blob/main/ps2xIOP/README.md) | Ghidra/entradas sintéticas e catálogo de serviços/imports IRX; comparar tratamento de módulos e RPC | `ps2n-dev` já aceita raízes e CSV. Automatizar essa passagem; virtual kernel/HLE é outra fronteira de execução, não uma substituição transparente da BIOS |
| [LightVelox/PS2Recomp, VIF](https://github.com/LightVelox/PS2Recomp/blob/main/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp) | UNPACK escalar/SIMD, seleção por formato e manutenção de lanes | Adaptar somente após demanda medida, com caminho portátil, wrap, máscaras, ROW/COL e bytes desconhecidos. Não impor SSE/AVX a ARM64 |
| [sotc-vibe-pc, benchmark VU1](https://github.com/LightVelox/sotc-vibe-pc/blob/main/Port/tools/vu1_bench.cpp) e [método de performance](https://github.com/LightVelox/sotc-vibe-pc/blob/main/Docs/PERFORMANCE.md) | Restauração por invocação, comparação VU, fila limitada e separação entre medição e hashes | Adaptar ao capturador VU1 existente. Não reutilizar endereços, estados, hipóteses de HLE ou timings da versão PAL no nosso disco USA |
| [ps2-web-recomp, gerador](https://github.com/snuri00/ps2-web-recomp/blob/main/recomp/codegen.cpp) e [fontes runtime](https://github.com/snuri00/ps2-web-recomp/tree/main/runtime/src) | Outro AOT R5900; inventário de DMA/VIF/IOP/IPU e chamadas/tail calls | Forte candidato a doador de rotinas isoladas. `clang::musttail` precisa de alternativa MSVC; não importar JIT ou kernel alternativo por arrasto |
| [ps2-native-recomp, autotests](https://github.com/exoteric33/ps2-native-recomp/blob/main/scripts/autotests.py), [replay](https://github.com/exoteric33/ps2-native-recomp/blob/main/README.md#saves-and-checks) e [speedcheck](https://github.com/exoteric33/ps2-native-recomp/blob/main/scripts/speedcheck.py) | Executor de resultados esperados, classificação de falhas e corpus de entrada reproduzível | Adaptar a comparação ao `ps2n-run`/`ps2n-boot`; o script chama seu próprio `port.sh`. Sua medição lê título de janela Windows e cenas GoW2: não copiar como contador genérico |
| [ps2autotests](https://github.com/unknownbrackets/ps2autotests) | Programas EE/IOP, DMA, GS, memória e VU acompanhados de `.expected` | Maior retorno imediato para confiança compartilhada. Começar pelo subconjunto que o loader/SIO já consegue executar; ELF/IRX com serviços de kernel ausentes deve ser bloqueado, não aprovado |
| [Play!, `Source/ee`](https://github.com/jpd002/Play-/tree/master/Source/ee) | DMAC, VIF, timers, tabelas IPU e testes/algoritmos pequenos | Interfaces CMIPS/Framework e HLE tornam a cópia integral cara. `Ee_IdleEvaluator` observa semáforos/threads: não é prova direta de polling MMIO; `FpMulTruncate` contém tratamento IEEE que exige conferir diferenças PS2 |
| [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs/blob/main/README.md) | Rastreamento de dependências, replay GS e investigação de shaders | Usar quando GS/fences forem gargalo ou houver falha visual. O próprio projeto declara limites de precisão de AA1/interpolação; outro renderer inteiro não resolve o EE lento |
| [GT4Recomp, arquitetura](https://github.com/moesuito/gt4-staticrecomp/blob/main/docs/ARCHITECTURE.md) | Registro AOT VU por identidade, fila assíncrona e separação de relógios | Relato específico de título/revisão; documentação de arquitetura e README têm escopos diferentes. Não tomar cobertura anunciada como prova sobre nossa VU ou sobre todos os jogos |
| [XenonRecomp](https://github.com/hedge-dev/XenonRecomp#optimizations) e [ReXGlue](https://github.com/rexglue/rexglue-sdk) | Metadados de função, redução de acessos ao contexto e toolkit reaproveitado | Referências de organização/otimização; instruções PPC, ABI e hardware Xbox não são implementações para PS2. Nenhuma alteração em x360-native |

A unidade útil de cópia é uma rotina, tabela, teste ou transformação com contrato
identificado. Para cada candidata: fixar fonte, preservar origem/avisos, listar
dependências, adaptar ao estado comum, executar resultado independente e comparação
com o caminho anterior. Só integrar se reduzir trabalho ou custo total. Este estudo
não incorporou código externo nem alterou a licença do repositório.

### Skills: usar poucas e no ponto certo

A skill Ponytail local está disponível e cobre o fluxo necessário. Foram lidas
também as skills upstream [ponytail-audit](https://github.com/DietrichGebert/ponytail/blob/main/skills/ponytail-audit/SKILL.md)
e [ponytail-gain](https://github.com/DietrichGebert/ponytail/blob/main/skills/ponytail-gain/SKILL.md).
A primeira orienta mapear fluxos e priorizar problemas com caso concreto; a segunda
exibe números de um benchmark externo, sem medir este projeto. Instalar ambas não
produz FPS nem validação adicional por si só. `ponytail-review` é candidata para
revisão de incrementos; seu conteúdo não foi auditado nesta etapa.

Foi examinada [console-anywhere](https://github.com/Jakeschincariol/console-skill/blob/main/console-anywhere/SKILL.md):
serve para toolchain, planos e empacotamento de `ps2EntryRunner`/PS2Recomp. Não
serve diretamente ao núcleo/CMake do ps2-native e não otimiza o runtime. Adiar
instalação/empacotamento; reaproveitar apenas verificações que faltarem nas CLIs
existentes. Não criar uma skill que duplique `AGENTS.md` e os relatórios.

### Regras e mecanismos que estão custando avanço

| Regra/mecanismo atual | Diagnóstico e mudança recomendada |
|---|---|
| Só continuar quando um valor consumido estiver conhecido/provado | Preserva honestidade, mas torna pesquisa numérica e reset um caminho serial. Adotar resultados de testes de console e perfis de hipóteses explícitas já permitidos; não exigir nova medição física para um fato que já possui evidência pública aplicável. Hipótese continua hipótese |
| Boot original e núcleo próprio como contrato | Explica parte da distância para ports com HLE. Manter o objetivo e executar compatibilidade experimental por pontos completos separados, já existentes. Transformar o boot original em pré-requisito de toda otimização repete o bloqueio do encoder sem ajudar a cena medida |
| Conferência de código/IRQ/observação por instrução | O contrato é invalidar corretamente e preservar eventos. `AGENTS.md` já permite especializar, mover ou cachear validações; não exige repetir o mesmo trabalho. Implementar provas reutilizáveis com invalidação, nunca desligar as verificações |
| Hash de esquema derivado do texto dos cabeçalhos | `cmake/Checkpoint.cmake` filtra helpers/marcadores e normaliza trechos para evitar incompatibilidade artificial. A auditoria de helpers VU já interrompeu integrações 25/26. Planejar assinatura explícita do formato serializado e runtime separado, com teste de alteração de campo/tipo/ordem; não remover o guard nem aceitar qualquer hash |
| Exigir seis hosts/hardware em cada experiência | Não é o que a política pede: testes focados no incremento e matriz na revisão publicada. Fazer exploração local curta, integração coerente e só então CI. Os seis alvos permanecem compromisso de portabilidade |
| Arquivos de contexto com tarefas antigas ainda imperativas | `NEXT-TASK` contém “não trocar de título” e “primeira partida não demonstrada” de etapas anteriores; relatório atual já demonstra partida headless. Usar somente a fronteira atual por título; histórico não pode revogar o pedido novo |
| Nenhuma dependência/framework sem necessidade | Não é proibição de reutilizar código pronto. Aprovar pelo encaixe no contrato e custo de manutenção; copiar um subsistema inteiro acoplado pode custar mais que adaptar uma rotina |
| Core genérico, sem hacks por jogo, sem JIT | É uma escolha de produto que custa esforço, não burocracia dispensável. Perfis por hash, raízes observadas, metadados Ghidra e AOT offline já são permitidos. HLE de serviços ou outro motor exigem decisão arquitetural própria e não são premissa deste plano |

Não há evidência de que remover testes, proteção de saves, identidade regional,
direitos de autoria ou limites de execução acelere corretamente o produto. O
pedido de priorizar reaproveitamento foi aplicado ao estudo técnico; nenhum aviso
de autoria foi removido. Não houve mudança automática de `AGENTS.md`/`CLAUDE.md`.

### Ordem de execução proposta

| Ordem | Entrega pequena e verificável | Arquivos/fluxo existente | Critério de conclusão |
|---|---|---|---|
| 0 | Baseline integrado único do tabuleiro, revisão/diff e AOT efetivo identificados | `ps2n-dev`, cenário/checkpoint atuais, `launcher/CMakeLists.txt`, sessão Qt | Mesmo estado/frames/PCM/input; cinco pares sem compilação concorrente; sampling separado. Confirmar o executável do launcher antes de atribuir-lhe ganho |
| 1 | Redução do maior custo CPU identificado: guard/memória, despacho ou IOP | `aot.cpp`, `aot_runtime.cpp`, `ee_semantics.hpp`, `memory*.cpp`, `iop.cpp` | Uma candidata por vez; manter referência; ganho total pareado e ausência de divergências. Encadear blocos só se o despacho for relevante; preservar saídas arbitrárias |
| 2 | Primeiro lote de testes de hardware reaproveitados | `ps2autotests` + adaptação mínima ao runner e CTest atuais | Resultados `.expected` independentes, versão fixada, timeout e bloqueios explícitos; começar por ALU/branches/memória e VU conforme suporte, depois DMA/GS/IOP |
| 3 | Descoberta observada integrada na preparação | `ps2n-dev` → analisador → `--entry`/`--table` → partições existentes | Não depender de scripts avulsos em `out/`; rejeitar bytes divergentes/raízes fora da imagem; rebuild sem mudança reutiliza objetos; invalidar por identidade e gerar offline |
| 4 | Uma correção compartilhada de compatibilidade com efeito entre títulos | Perfis e checkpoints dos três jogos; DMA/SIF/CDVD/VU/GS conforme primeira recusa | Regressão própria antes da correção e novo limite real depois. Consultar a implementação doadora antes de escrever outra; demonstrar que os outros cenários continuam iguais |
| 5 | VU1/GS/SIMD e apresentação, conforme uma cena que os exerça | Captura VU1, compute Vulkan/D3D12 e frontend existentes | Invocação/primitivas realmente executadas; ganho integrado; portabilidade/capabilities; RGBA/PCM/IRQ/checkpoint. VU1 ociosa não recebe otimização comercial fictícia |
| 6 | Sessão jogável e expansão controlada do catálogo | Launcher com os mesmos módulos aceitos; perfis isolados | Entrada, som ouvido, saves quando suportados, pacing e avanço original em cena prolongada. Só depois classificar mais revisões como jogáveis |

As ordens indicam dependências e prioridade, não prazos prometidos. Testes de
compatibilidade podem avançar enquanto não houver medição concorrente; nunca
compilar e medir no mesmo host ao mesmo tempo. Evitar abrir várias modificações
de cabeçalhos/ABI em paralelo: primeiro concluir e congelar um incremento.

Na exploração usar a janela de 30 milhões para rejeitar candidatas rapidamente;
para aceitar desempenho, ampliar à sequência existente de movimento/disparo e
depois a uma sessão prolongada. Registrar tempo convidado/host, intervalos de
campos, travadas, saída de áudio e custo de geração/build. A meta é progressão
original e áudio contínuo, sem forçar 30 FPS ou PAL a 60 atualizações.

### Seleção de jogos e definição de suporte

| Título local | Evidência atual | Próxima prova que vale mais que contar instruções |
|---|---|---|
| Super Bust-A-Move | ROUND A1 com movimento/disparo em harness; equivalência de campos/PCM; cerca de 15× abaixo do tempo real na janela recente | Tornar o executável efetivamente usado mais rápido, depois validar sessão visível com som e controles |
| The Simpsons: Hit & Run | MFIFO/BC0 implementados; cenário de injeção e boot convidado têm bloqueios distintos; sem vídeo jogável | Identificar produtor do bit desconhecido após SCMD 0x15 no boot convidado; no cenário separado, resolver inicialização de QWC/CPC sem inventar bits |
| Shadow of the Colossus | Contadores, transferências, campos não-pretos e checkpoints registrados; não certificam uma cena controlável | Identificar visualmente o conteúdo e sua resposta à entrada; capturar VU real se executada; medir progresso audiovisual da mesma cena |

Manter uma linha por hash/revisão/cenário com níveis distintos: identificado,
preparado, boot, menu, partida controlável, sessão validada e desempenho medido.
Uma correção genérica deve ser reaplicada aos três casos, mas não promover todos
ao mesmo nível de suporte. Só adicionar outro título quando testar um recurso
que esses três não exercitem ou compartilhar um bloqueio cuja correção esteja
pronta. Tempo para localizar/corrigir a primeira falha e regressões entre títulos
são métricas de desenvolvimento mais úteis que bilhões de instruções retiradas.

### Limites desta entrega

Conferência estática e reanálise de resultados existentes; nenhum teste C++,
benchmark novo, sessão humana, coleta sampling ou CI remota foi executado.
Foram conferidos os cinco pares recentes e a igualdade de campos/PCM/input/GS/
estado nos JSONs de validação correspondentes. Não se provou ganho novo nem
compatibilidade adicional. O próximo incremento recomendado é a ordem 0 e,
com seu perfil, uma única otimização estrutural da ordem 1.

---

## Comparação inicial — 03/11/2026 (histórico)

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
