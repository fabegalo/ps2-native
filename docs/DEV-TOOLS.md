# Ferramentas de desenvolvimento

As CLIs são C++20/CMake, sem Python, Qt, admin ou AVX obrigatório. Usam o
mesmo núcleo e as mesmas semânticas de `ps2n-aot`, `ps2n-run`, `ps2n-boot`
e `ps2n-disc`. ELF direto continua sendo um ambiente bare-metal explícito;
ele não instala serviços de BIOS nem comprova boot ou jogabilidade.

## Cenário reproduzível

```sh
cmake --build out/build --config Release --target ps2n-dev ps2n-debug ps2n-analyze
ps2n-dev init /caminho/do/jogo.iso out/dev-local/jogo.ps2n
ps2n-dev run out/dev-local/jogo.ps2n
ps2n-dev replay out/dev-local/jogo.ps2n
```

Use o diretório `Release` do build quando o gerador for multiconfiguração.
`init` reconhece ELF, ROM0 e ISO/BIN aceitos pelo loader comum; `--kind=elf`,
`--kind=bios` e `--kind=disc` tornam o formato explícito. BIOS é validada
pelo ROMDIR/ROMVER; disco é validado por ISO 9660/BOOT2. Não há busca de
firmware ou dependências em outras pastas.

O perfil é texto UTF-8, uma linha `chave=valor`, sem aspas de shell.
Linhas iniciadas por `#` são comentários; uma opção com espaços continua
sendo **um argumento**, inclusive no Windows. Caminhos são resolvidos
contra a pasta do perfil e congelados como absolutos no cenário salvo.

```ini
schema=ps2n-dev-v1
name=diagnostico-local
kind=elf
input=/dados/meu.elf
source=/projetos/ps2-native
build=/projetos/ps2-native/out/dev-local/build
cmake=/caminho/cmake
config=Release
engine=aot
timeout=300
ring=256
stream_bytes=0
runtime_arg=--budget=1000000
runtime_arg=--stop=0x00100020
aot_arg=--entry=0x00100100
```

`init` já preenche caminhos, SHA-256 do input, orçamento e um build isolado
em `out/dev-tools`. Para BIOS, use opções reais do runner, por exemplo
`runtime_arg=--console=MODELO:PLACA`, `runtime_arg=--nvm=/dados/console.nvm`
e `runtime_arg=--max-instructions=1000000`. As hipóteses experimentais
continuam opções explícitas; nenhuma é ativada pelo orquestrador.

`generator` e `architecture` selecionam o gerador e a plataforma CMake;
`cmake_arg=-D...` permite definições repetidas de compilador/toolchain.
Com Ninja/MSVC, execute no ambiente de desenvolvimento do compilador.
`engine=interpreter` permite comparar com o motor sem AOT.
`aot_arg` aceita somente raízes `--entry` e tabelas `--table` explícitas.

O build exclusivo é travado durante a execução; um build já existente sem
identidade do `ps2n-dev` é recusado. Núcleo, cabeçalhos, tradução e runner
pertencem ao mesmo grafo CMake. Mudança de fonte durante a compilação é
recusada antes de executar. Repetições usam dependências incrementais;
os artefatos AOT são separados por perfil, SHA-256 do input, opções e
gerador efetivamente compilado. O hash da tradução é conferido antes do
reuso. Não se copiam ISO, BIOS, árvore de fontes ou tradução AOT.
Orçamento, tamanho do anel e streaming não invalidam análise/AOT. As
revisões do analisador e do gerador possuem chaves independentes; mudar
somente o relatório de análise não regenera a tradução. O dono original
do build preserva o isolamento do perfil também no replay de snapshots.
O AOT gerenciado usa partições estáveis por faixas de 64 KiB de endereços
convidados. Cada unidade C++ tem nome pelo SHA-256 do próprio conteúdo;
o dispatcher tem caminho estável dentro do perfil. Mesmo quando uma nova
versão do gerador exige repetir a geração, unidades idênticas mantêm
caminho, timestamp e objeto compilado. O manifesto confere o dispatcher
e **todas** as unidades antes do reuso; corrupção exige regeneração.

Para builds manuais, `ps2n-aot meu.elf modulo.cpp --hybrid --split` emite
`modulo.cpp`, unidades `ps2n_aot_<sha256>.cpp` e `modulo.cpp.sources`.
`--split=BYTES` seleciona outra faixa, potência de dois e ao menos quatro
bytes. Continue passando `modulo.cpp` a `PS2N_ELF_AOT_SOURCE` ou
`PS2N_BIOS_AOT_SOURCE`; a integração CMake valida e compila as unidades
listadas, em paralelo, sem glob de arquivos antigos. O manifesto é dado,
não código CMake. A CLI sem `--split` conserva a saída única anterior.
Não mover apenas o dispatcher: o conjunto e seu manifesto são necessários.

Cada rodada guarda `profile.ps2n`, `identities.txt`, `manifest.json`,
commit Git, estado/diff local, hash do runtime, configuração CMake e
identidade/flags do compilador, argumentos, logs separados, tempos e
espaço lógico do build. `replay PERFIL` usa o último cenário
concluído; `replay DIRETORIO_DA_RODADA` seleciona outro cenário salvo.
Inputs e NVM/IOP explícitos precisam continuar com os hashes gravados.
Uma correção de runtime pode ser recompilada e fica registrada como nova
revisão. Replay executa desde o reset, **não** restaura um save-state.

## Diagnóstico e comparação

```sh
ps2n-run meu.elf --budget=1000000 --diagnostic-dir=out/diagnostico
ps2n-boot minha-rom0.bin --diagnostic-dir=out/boot
ps2n-debug report out/diagnostico
ps2n-debug compare out/antes/events.ndjson out/depois/events.ndjson
ps2n-debug log out/trace-antigo.log 32
```

`--diagnostic-ring=N` define o número de eventos retidos (1 a 1 milhão).
O observador compartilhado registra tentativas de instrução, operandos
GPR e suas máscaras desconhecidas, acessos de barramento, eventos de
sistema e eventos dos históricos IOP/GIF/vídeo. Esses históricos e o
trace textual ficam limitados durante o diagnóstico. Desligado, o modo
mantém o comportamento anterior e não formata eventos adicionais.

`report.json` identifica a primeira recusa explícita do runtime: PC,
opcode, mnemonic, delay slot, categoria, motivo e contadores de AOT e
interpretação. `state.json` inspeciona EE/COP0/FPU, GPR/COP0 do IOP com
incerteza preservada, vetores VU0, estado VIF, DMA EE, FIFOs SIF e
contadores GS. `--diagnostic-ram=0xENDERECO:BYTES` acrescenta até 16
janelas físicas de backing RAM, até 4096 bytes cada, sem acesso MMIO,
sincronização ou alteração do convidado. Scanout final produz `frame.bmp`
e hash RGBA quando o GS permite; indisponibilidade guarda o motivo real.
Não há imagem sintética para substituir uma captura indisponível.

O histórico circular declara quantos eventos anteriores foram descartados.
Comparações entre anéis só são aceitas com a mesma cobertura. O resultado
é a **primeira divergência registrada**, não uma prova sobre eventos já
descartados ou dispositivos não inspecionados. Contadores de motor não
participam da comparação semântica, permitindo comparar AOT e interpretador.

Para comparar desde o início, habilite explicitamente
`--diagnostic-stream-bytes=104857600` (ou `stream_bytes` no perfil).
`full-events.ndjson` é escrito durante a execução, com limite de bytes.
Ao atingir o limite a execução continua e o stream é marcado truncado;
um stream sem término válido ou truncado nunca compara como completo.
Essa modalidade tem custo de formatação/I/O por evento; o anel é o padrão.
O comparador lê sequencialmente, sem carregar o trace inteiro em memória.

`ps2n-debug log` extrai o primeiro registro explícito `STOP`/`block point`
ou a recusa terminal do harness legado e uma cauda limitada de logs antigos.
Não infere deadlock por PC repetido
nem transforma warnings em causas comprovadas.

## Análise estática

```sh
ps2n-analyze meu.elf out/analise --ghidra=out/funcoes.csv
ps2n-analyze jogo.iso out/analise-disco --disc
ps2n-analyze minha-rom0.bin out/analise-bios --bios
ps2n-analyze modulo.irx out/metadados-iop --irx
```

`analysis.json`, `analysis.txt` e `cfg.dot` exportam o CFG conservador já
usado pelo AOT: raízes e sua evidência, símbolos e tamanhos declarados,
relocations sem aplicação, chamadas por callsite, destinos indiretos,
jump tables provadas, fronteiras de interpretação e lacunas AOT.
As arestas preservam a distinção entre transferência, delay slot e
anulação de branches likely. Uma decodificação conhecida não prova
que todos os operandos e dispositivos serão aceitos em runtime.
Bytes executáveis não visitados podem ser dados; não viram funções por
heurística. Não se inferem limites de função nem regiões dinâmicas de
conteúdo desconhecido. A análise falha explicitamente se a prova de CFG
for inválida ou exceder o limite do analisador compartilhado.

O CSV opcional usa o formato `Name,Start,End,Size` do exportador Ghidra
estudado, com `End` exclusivo. Endereços, alinhamento, limites e tamanho
são conferidos; nomes e extents externos continuam indicações, sem
patches ou stubs. No perfil, `ghidra=/caminho/funcoes.csv` vincula o mapa
por SHA-256 e passa as mesmas raízes validadas para análise e AOT.
Ghidra não é dependência de build ou execução.

`--irx` verifica o contêiner ELF32/MIPS e identifica as seções de módulo
`.iopmod`/tipo `0x70000080`, com posição, tamanho e SHA-256. O layout da
identidade interna não é decodificado e não há CFG R5900, relocação
aplicada ou linker/executor IRX nesse modo. Isso fica explícito no JSON.
A referência dos identificadores é o
[ps2sdk](https://github.com/ps2dev/ps2sdk/blob/d41ea62946e53d9a0c8788bb9eb8c8faf488b559/tools/ps2-irxgen/src/elftypes.h);
nenhum código desse projeto foi incorporado.

O fluxo `ps2n-dev` executa `ps2n-analyze` antes da geração e confere os
hashes de todos os relatórios antes de reutilizá-los. A pasta de análise
é referenciada por `analysis-reference.txt`; não é copiada a cada rodada.
A tradução compilada é referenciada por `aot-reference.txt` no modo AOT.

## Validação

`dev-diagnostics` verifica SHA incremental, anéis limitados, equivalência
AOT/interpretador, primeira divergência e recusa de streams incompletos,
malformados ou truncados. `dev-analysis` verifica CFG, hints Ghidra,
instruções desconhecidas e metadados IRX. `dev-cli-pipeline` executa
ELF→análise→AOT→build→diagnóstico→replay, corrupção do cache, identidade
alterada, ISO/BOOT2 e BIOS próprias; verifica ainda timeout de processo,
build sem alterações e recompilação com fonte/cabeçalho alterados apesar
de timestamps restaurados. As identidades de conteúdo também participam
do comando de compilação nos geradores Visual Studio, Ninja e Makefiles.

Medições locais Windows x64/Release/MSVC/Ninja, mesma fixture própria e
mesmos parâmetros, em 04/11/2026. Commit `392acc8`, checkout de verificação
fixo e árvore Git limpa, identificados nos manifestos. Dados em `out/`,
não versionados; nenhum firmware/jogo foi copiado para esse checkout:

| Medida | Primeira rodada, build novo | Replay |
|---|---:|---:|
| Fluxo completo até a mesma recusa | 64,49 s | 1,18 s |
| Build do runtime/AOT | 4,152 s | 0,181 s, zero recompilações |
| Processo de execução, incluindo inicialização/I/O | 0,161 s | 0,087 s |
| Reutilização de análise e AOT | não | sim, hashes conferidos |
| Espaço lógico do build | 27.825.302 bytes | 27.865.711 bytes |

O executável manteve o mesmo SHA-256 e ambas as execuções retiraram duas
instruções antes da mesma recusa MMI. O ganho está na preparação/build;
os tempos de processo incluem variação de startup e cache do host e não
demonstram aceleração da CPU convidada. A rodada seguinte acrescentou
40.409 bytes de evidência; não duplicou input, análise ou tradução.
Comparação completa das duas execuções: 112 ms incluindo startup da CLI.
Alterar orçamento/anel/stream preservou os três artefatos; repetir a
geração após remover o metadado de chave reutilizou o mesmo C++/timestamp
e manteve o build sem compilação (0,220 s). Corrupção real da tradução
foi detectada e exigiu regeneração/rebuild, como esperado.

Trace sintético de 1.000.001 linhas/40.000.081 bytes, três amostras
alternadas: mediana de 0,511 s no resumo `ps2n-debug log`, produzindo
1.734 bytes, e 0,082 s no `rg` buscando a recusa terminal. **Sem ganho
em busca bruta**: os comandos produzem saídas diferentes; o resumo agrega
causa, posição e cauda limitada. Não foi medido tempo humano poupado.

Também foram verificados: 17 regressões focadas de boot/AOT/IOP/GIF/GS/VIF
e ELF direto; fluxo `ps2n-dev` com ROM0 local real por 100.000 instruções
sem hipóteses adicionais (parada pelo orçamento, não boot concluído);
extração da recusa causal de um log local real existente; frame próprio
64×32 por VIF1→GIF→GS, hash RGBA
`402316200a4786b2d6d81a071e89e2adbd98f25d804583790329ae3cb6f436a3`.
Fixtures e capturas sintéticas não certificam Browser, menu ou gameplay.
A CI existente inclui os novos testes nos seis alvos nativos.

## Checkpoints e interface visual

Não existe serialização/restauração completa do runtime. RAM não inclui
caches e domínios simbólicos, estado em trânsito de EE/IOP, VU/Q, GS/VRAM,
DMA/FIFOs, relógios, interrupções, CDVD, SPU2, SIO2 ou periféricos.
Portanto estas ferramentas **não oferecem checkpoint de RAM**.
Um save-state futuro exige identidade completa do executável, firmware,
perfil e versão de runtime, além de prova contínuo versus retomado com
estados, sequência de eventos e frames. O objetivo imediato é reduzir
build, preparação e leitura de logs; não reduzir instruções por atalhos.
Não foi criada outra interface visual ou outro launcher Qt.

## Referências e licença

Consulta de comportamento e fluxo, sem copiar, adaptar ou vincular GPL:
[Analyzer](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xAnalyzer/src/analyzer_main.cpp),
[passes](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xAnalyzer/src/analysis_passes.cpp),
[Ghidra](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/tools/ghidra/ExportPS2Functions.java)
e [Studio](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xStudio/src/main.cpp).
O código novo permanece BSD-3-Clause e reutiliza o CFG conservador local;
não importa bancos de símbolos comerciais, stubs ou patches por título.
Perfis, estados, logs e traduções de conteúdo do usuário ficam locais e
ignorados pelo Git. Não publicar artefatos derivados de jogos/firmware.
