# Decisão de licença — 2026-11-03

## Incorporação HLE do PS2Recomp — decisão de 07/11/2026

O mantenedor autorizou incorporar o código e validou a adaptação do kernel HLE
ao estado único do ps2-native, incluindo os termos GPL da distribuição combinada.
`third_party/ps2recomp/hle_kernel.cpp` deriva de `EeScheduler.cpp`,
`Syscalls/{Dispatcher,Thread,Sync,System}.cpp` e `Syscalls/Helpers/State.h` da
revisão `e5636981a1c9de368f6b71e819c3dc188dfedbf4`. Licença integral e origem
estão em `third_party/ps2recomp/{LICENSE,NOTICE}`; créditos do upstream preservados.

O código original mantém seus avisos BSD-3-Clause. Os binários que ligam este
componente GPL devem ser distribuídos sob os termos GPL-3.0, com código-fonte
correspondente e avisos aplicáveis; não são uma distribuição exclusivamente BSD.
As decisões M0 abaixo são históricas e anteriores a esta incorporação.

Não foram importados outro runtime/memória/GPU, overrides por jogo ou stubs de
sucesso. A primeira adaptação cobre syscalls de threads, semáforos e eventos,
SetupThread, SetupHeap/EndOfHeap, GetMemorySize e FlushCache (adaptada ao cache
compartilhado, sem usar o stub de sucesso do upstream). Serviços ausentes são
recusados explicitamente.

## Reutilização efetiva: leitor do IPU — 07/11/2026

A rotina `bits_at` de [ps2-web-recomp, revisão
e026b971324068773eed88bf502437de9e581b6b](https://github.com/snuri00/ps2-web-recomp/blob/e026b971324068773eed88bf502437de9e581b6b/runtime/src/ipu/ipu.cpp)
foi copiada e adaptada em `include/ps2native/ipu.hpp`: troca a fila global pela
fila da instância e acrescenta `const`. A extração MSB-first de até oito bytes
mantém o algoritmo original; o chamador local verifica a disponibilidade total
antes de publicar a tabela. Copyright Samir Nuri, MIT, preservado em
`third_party/ps2-web-recomp/LICENSE`. Não adiciona dependência externa.

O despacho, retenção da qword parcial, limites da FIFO, estado desconhecido,
IRQ, checkpoints e testes usam os contratos existentes do ps2-native. Não
foram importados o decoder MPEG completo, HLE, ordenação zigzag interna do
doador ou suposições de temporização. As tabelas locais mantêm ordem de entrada.

**Decisão para o M0:** BSD-3-Clause para código original independente (`LICENSE`). Nenhum código de GPL foi copiado, ligado, distribuído ou incorporado. Nenhuma dependência C++ externa foi adicionada. A matriz CI usa `actions/checkout` fixado no SHA da versão v4.2.2.

| Projeto consultado | Arquivos oficiais verificados | Licença declarada | Uso no M0 |
|---|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | `LICENSE`, `ps2xRecomp/src/lib/elf_parser.cpp`, `control_flow_analyzer.cpp`, `vu_translator.cpp` | GPL-3.0 | consulta arquitetural, sem integração |
| [PCSX2](https://github.com/PCSX2/pcsx2) | `pcsx2/Docs/License.txt`, `pcsx2/Interpreter.cpp`, `pcsx2/Memory.cpp` | GPL-3.0 ou posterior | referência comportamental, sem integração |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | repositório/licença | MIT | referência de conceito, sem código |
| [Play!](https://github.com/jpd002/Play-) | `License.txt`, `README.md` | licença permissiva de 2 cláusulas | referência arquitetural, sem código |

**Launcher desktop (opcional):** usa o [Qt 6](https://www.qt.io/licensing/open-source-lgpl-obligations) — módulos Core, Gui, Qml, Quick, QuickControls2, Svg e o plugin de formatos de imagem — sob **LGPL-3.0, ligado dinamicamente**, de modo que o usuário pode substituir as bibliotecas Qt. Nenhum código Qt é copiado; nenhum módulo disponível apenas sob GPL (Qt Charts, Qt Graphs, Qt Quick 3D, Qt Virtual Keyboard e afins) é usado; o núcleo e as ferramentas de linha de comando continuam sem dependência do Qt (`PS2N_LAUNCHER=AUTO|ON|OFF`). Uma distribuição futura do launcher deverá cumprir as obrigações da LGPL (texto da licença, aviso, bibliotecas substituíveis, sem ligação estática não autorizada); hoje não há distribuição. Ícones e arte do launcher são originais; o banner é o de `docs/assets`. Detalhes técnicos: [LAUNCHER.md](LAUNCHER.md).

Código GPL incorporado/derivado/ligado ao programa pode impor requisitos GPL sobre o trabalho resultante e obrigações de publicação do código correspondente, incompatíveis com apresentá-lo puramente como BSD. Antes de qualquer integração, decidir explicitamente: adotar GPL quando necessário, obter autorização compatível, ou usar uma implementação original independente. Distribuição separada de executáveis possui nuances jurídicas e não é uma exceção universal ao copyleft; **não presumir compatibilidade**. Revisão jurídica externa é apropriada antes de distribuição comercial.

Todos os artefatos AOT derivados de jogos continuam sujeitos aos direitos sobre o código de entrada. Não distribuir jogos, imagens, ELF, BIOS, fontes, logos, boot animations, assets proprietários ou traduzir e publicar código do usuário sem permissão.

**Saída PCM experimental (05/11/2026):** o backend Windows usa APIs do
SDK do sistema (`waveOut`/Winmm e XInput), sem incorporar um sintetizador
externo. Nos outros hosts, Qt Multimedia é opcional e ligado dinamicamente
sob LGPL-3.0, conforme a [documentação do módulo](https://doc.qt.io/qt-6.8/qtmultimedia-index.html#licenses-and-attributions).
Somente `QAudioSink` recebe PCM produzido pelo SPU2 do projeto; não há
decodificação de mídia nem código de emuladores externos neste backend.
As mesmas obrigações de uma futura distribuição Qt continuam aplicáveis.

Fontes consultadas em 03/11/2026:
- https://github.com/ran-j/PS2Recomp/blob/main/LICENSE
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/License.txt
- https://github.com/N64Recomp/N64Recomp/blob/main/LICENSE
- https://github.com/jpd002/Play-/blob/master/License.txt

## Auditoria do fast track M3.0

Consulta em 03/11/2026. Nenhuma integração de fonte, patch, biblioteca ou executável externo foi feita.
As implementações Direct ELF, Count/Compare, SIF1, VIF1 e overlays são originais BSD-3-Clause.

| Referência | Evidência auditada | Decisão |
|---|---|---|
| [gow-native-pc](https://github.com/Vnnc1/gow-native-pc/blob/d28a63d3fd0f2a412ff9018b52d2a9a3cbdb18fd/CREDITS.md) | LICENSE/CREDITS GPL-3.0; `engine/runtime.toml` fixa runtime Tobiichi; `games/gow1/game.toml` lista mapa, overrides e geradores | Arquitetura de pipeline como referência; não incorporar launcher, patches, runtime ou overrides |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/LICENSE) | GPL-3.0; README e `ps2xIOP/README.md` distinguem compilador, runtime, IRX e HLE | Referência de organização; nenhuma cópia, tradução de fonte ou vínculo |
| [Play!](https://github.com/jpd002/Play-/blob/1f27232772e484bfb79364b751b84f41f4c87481/License.txt) | Licença permissiva de duas cláusulas; interfaces DMAC/INTC/SIF/VIF/VU/GS e IopBios consultadas | Reutilização potencial exige manter aviso/disclaimer e auditar dependências por arquivo. Não houve incorporação: adaptação de componentes completos traria CMIPS, JIT/Framework e serviços HLE acoplados |
| [PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/License.txt) | GPL-3.0-or-later verificada novamente | Continua referência comportamental; nenhuma integração |

O payload R3000A próprio desta etapa não implementa o formato/linker IRX e não usa módulos de Sony.
O código `ps2sdk/iop/system/sifman/src/sifman.c` foi consultado como fonte adicional de pesquisa;
seu cabeçalho declara AFL-2.0 e referência a SDK SCE. Nenhum trecho foi incorporado ou usado como
base de implementação; esta consulta não é aprovação de reutilização de seus arquivos.
