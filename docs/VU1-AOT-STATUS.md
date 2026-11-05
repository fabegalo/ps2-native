# VU1 AOT — evidências e limites

## Integração da primeira versão

`ps2n-aot --vu1` acrescenta o alvo VU1 à ferramenta offline existente. A
imagem é uma observação integral dos 16 KiB, com valores e máscaras de bits
desconhecidos. A identidade SHA-256 inclui essa observação, a versão do
contrato nativo e o modo aritmético experimental quando explicitamente usado.
O formato próprio conserva os bits desconhecidos; um dump cru de 16 KiB
significa que todos os bits são conhecidos. As entradas usam PCs em bytes.

O CFG conserva branches diretos, delay slots, término E e wrap da memória;
destinos indiretos sem prova e instruções fora do subconjunto compartilhado
continuam fronteiras explícitas do interpretador. Dependências VF/VI/ACC/Q
e flags são descritas pelo analisador; os interlocks e writebacks continuam
no escalonador compartilhado, sem antecipar seus efeitos.

Cada par traduzido instancia a semântica C++20 comum com palavras constantes.
Isso permite ao compilador host especializar opcodes, operandos e máscaras
sem duplicar a implementação aritmética nem chamar o interpretador como
substituto do corpo nativo. Não há fast-math global, exigência de AVX,
aproximação nova de ponto flutuante ou backend GPU nesta versão. Não foi
introduzido um caminho SSE/NEON separado: otimizações futuras dependem de
equivalência demonstrada com as operações PS2 existentes.

O registro automático aceita descritores de traduções já ligadas ao
executável. A seleção confere SHA-256, modo e todos os valores/máscaras;
escritas MPG e pela janela de microcódigo invalidam a seleção antes da
próxima emissão. Ausência de programa ou PC traduzido usa fallback
identificado e contado. O cache e os contadores são estado do host,
excluídos dos checkpoints; a restauração revalida os bytes serializados.

Partições reutilizam `AotSources`, escrita idempotente, nomes por conteúdo,
manifesto verificado e `ps2n_target_aot_source` do AOT EE. O parâmetro CMake
`PS2N_VU1_AOT_SOURCES` liga os dispatchers offline aos runners existentes.
Não há invocação de compilador C++ durante a execução convidada.

Limite do cache desta primeira versão: a identidade integral da imagem
participa do namespace das funções geradas. Alterar qualquer palavra exige
uma nova imagem/namespace e pode recompilar todas as suas partições; não
foi demonstrado reuso incremental por subfaixa de microcódigo modificado.
As unidades sem mudanças de conteúdo e o build sem alterações continuam
reutilizáveis pelo mecanismo existente.

Exemplo de geração local, após uma captura própria válida:

```sh
ps2n-aot captura.ps2nvu out/vu1/translated.cpp --vu1 --entry=0x0 --split=1024
cmake -S . -B out/build -DPS2N_VU1_AOT_SOURCES=/caminho/absoluto/out/vu1/translated.cpp
```

As raízes desse exemplo são ilustrativas; usar os PCs realmente observados
ou provados para cada captura, sem adivinhar entradas. `Memory::set_vu1_capture`
oferece observação opcional dos inícios reais e das mudanças de microcódigo
pendentes, incluindo entregas por VIF MPG. A captura permanece local ao
perfil; uma imagem privada não deve entrar no controle de versão.

## Consulta das referências — 05/11/2026

Consulta conceitual, sem copiar, adaptar, traduzir ou vincular código externo.
As três referências declaram GPL-3.0; a implementação do ps2-native deve
continuar original sob BSD-3-Clause. Fontes traduzidas de microprogramas
comerciais, capturas e checkpoints ficam apenas nos diretórios locais ignorados.

O caminho solicitado `ps2xTest/tools/vu1_recomp.cpp` não existe na raiz da
revisão atual de sotc-vibe-pc. Esse projeto fixa `External/PS2Recomp` como
submódulo; o arquivo existe nesse submódulo, em LightVelox/PS2Recomp.

| Fonte primária e revisão consultada | Evidência e técnica conceitual |
|---|---|
| [sotc-vibe-pc `1ce4a5e`, `.gitmodules`](https://github.com/LightVelox/sotc-vibe-pc/blob/1ce4a5ea4955878bd2edabdc4e1a6b6f49a8e1ad/.gitmodules) | Submódulo PS2Recomp; a árvore fixa `d06a4ab4a6902e0ae324f184c22581c3aa387c36` |
| [Gerador VU1 `d06a4ab`](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xTest/tools/vu1_recomp.cpp) | Entrada de imagens de 16 KiB; identificação FNV64, registro de funções por imagem, blocos e unidades C++; análise de pares upper/lower e dependências por lane |
| [Contrato de pipeline VU1 `d06a4ab`](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/include/runtime/ps2_vu1.h) | Filas explícitas de VF, VI, ACC, flags, stores, Q/P; interlocks, contador de geração do microcódigo e PATH1 |
| [Runtime compartilhado `d06a4ab`](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/vu/ps2_vu1_impl.inl) | Execução compilada com saída para continuação/budget/fallback; verificação contra interpretação e observação dos pacotes GIF |
| [Replay/benchmark de sotc `1ce4a5e`](https://github.com/LightVelox/sotc-vibe-pc/blob/1ce4a5ea4955878bd2edabdc4e1a6b6f49a8e1ad/Port/tools/vu1_bench.cpp) | Restaura registradores e pipelines antes de cada chamada; aplica diferenças da memória VU; mede chamadas/ciclos/campos e verifica a execução compilada |
| [PS2Recomp original `e563698`](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp) | Referência separada de decodificação, uso de registradores e escalonamento de pipeline; não confundir tradutor macro COP2 com gerador AOT de microprogramas |
| [Licença sotc](https://github.com/LightVelox/sotc-vibe-pc/blob/1ce4a5ea4955878bd2edabdc4e1a6b6f49a8e1ad/LICENSE), [licença do fork](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/LICENSE), [licença original](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/LICENSE) | GPL-3.0; uso apenas como referência de comportamento/arquitetura |

O gerador consultado distingue blocos com upper/lower simples de fronteiras
com EFU, XGKICK e operações genéricas. Tem saídas conservadoras para
instruções reservadas, D/T e combinações sem prova de branch/end e delay
slot. O estado anterior dos operandos do par é preservado; a propagação de
ACC é distinta da publicação das flags FMAC. O caminho de referência mantém
Q/P e writebacks pendentes em vez de publicar resultados antes do ciclo devido.

As otimizações conceituais pertinentes são especialização de operandos,
eliminação da decodificação em runtime, acesso direto a VF/VI/ACC, fluxo
entre blocos e retirada de verificações redundantes somente com prova de
dependência. O emissor SSE, suas aproximações/atalhos FMAC, seu formato de
captura e seu cache não são base de implementação para este projeto.

## Condições para a evidência comercial local

Há checkpoints completos locais de Super Bust-A-Move com dependências
fixadas pelo manifesto da sessão. São dados privados existentes; sua
presença não comprova que a janela escolhida execute VU1. O jogo já exercita
COP2/VU0 macro e VIF1/GIF PATH2, que não contam como execução de microcódigo VU1.

Uma captura válida deve conservar os bits desconhecidos: `micro1_` nasce
com 4096 palavras não escritas e MPG pode preencher só parte dos 16 KiB.
Usar zero como conteúdo dessas palavras fabricaria uma imagem que o convidado
nunca carregou. O dump deve registrar valor e máscara, e somente os pares
comprovados podem entrar na tradução. A identidade deve incluir ambos.

O procedimento de validação é carregar o checkpoint pelo loader comum,
verificar bindings de firmware/NVM/disco e cenário, capturar o microcódigo
sem acesso de barramento nem avanço do relógio e observar inícios/contadores
VU1 durante uma janela limitada. Só depois gerar/compilar offline a imagem
capturada e repetir exatamente a mesma janela com e sem AOT VU1. Conferir
estado completo, ciclos, interrupções, bytes RGBA e PCM; contagem de
instruções nativas deve ser positiva para declarar um microprograma real
executado pelo código recompilado.

## Verificação comercial inicial da janela disponível

O harness local recompilou os fontes EE AOT existentes contra os cabeçalhos
do núcleo VU1, sem reutilizar objetos com layout anterior de `Memory`. Ele
reutiliza o manifesto, firmware, disco e checkpoint completos existentes;
nenhum original foi alterado e nenhuma sessão gráfica foi aberta.

No checkpoint de menu e após 30.000.000 instruções EE, a memória VU1 contém
somente 2048 repetições do par sentinela NOP/NOP com T. Não houve início
MSCAL/MSCNT, instrução VU1 interpretada ou nativa, ou seleção de tradução.
A imagem integral conhecida é um estado válido de memória, **não uma
captura de invocação de microprograma comercial**. Não a executar
artificialmente para apresentar cobertura comercial inexistente.

As primeiras comparações de 1 e 30 milhões conservaram o estado completo
contra o executável anterior, tanto com AOT VU1 habilitado quanto forçando
sua interpretação. Na janela longa: 25.446.236 instruções EE AOT e
4.553.764 interpretadas, seis campos RGBA e 9.766 amostras PCM idênticos.
Essas execuções iniciais servem para equivalência; suas durações coincidiram
com builds e não fundamentam ganho de desempenho. A repetição final deve
usar o núcleo congelado e rodadas sequenciais sem compilação concorrente.

Permanece pendente capturar uma invocação VU1 real de um jogo que use essa
unidade, gerar sua tradução offline e medir o ganho integrado nessa carga.
O menu disponível não autoriza alegar ganho comercial VU1, FPS comercial
ou gameplay. A prova nativa desta primeira versão depende dos programas
próprios, com saída GS observável e equivalência completa documentada nos
resultados finais abaixo.

## Testes e desempenho da fixture própria

Quatorze testes locais focados passaram no núcleo final, cobrindo VU,
VIF, AOT EE, GS/PATH1 e checkpoints. O teste novo compara a execução
interpretada e o C++ gerado efetivamente compilado em fronteiras de ciclo,
incluindo VF/VI/ACC, Q, flags, writebacks pendentes, memória, branches,
delay slots, wrap, MPG, microcódigo alterado, tradução ausente e fallback.
O caminho integrado exerce EE AOT, interrupção Count/Compare, VIF/GIF/GS,
pixels RGBA observáveis e retomada de checkpoint completo. A geração
particionada também foi compilada em cinco unidades mais dispatcher e
passou o mesmo teste; não foi validada somente pela inspeção dos arquivos.

P permanece preservado como parte do estado/controladores, mas esta
versão **não executa EFU nem valida operações/latências produtoras de P**.
I/M/D/T, destinos indiretos sem prova e operações fora do subconjunto
compartilhado continuam recusas/fronteiras explícitas. Limites aritméticos
do interpretador continuam valendo para o AOT; a equivalência não os converte
em precisão de hardware certificada.

Host Windows x64, AMD Ryzen 7 5700X3D, MSVC 19.44 Release. Três
rodadas alternadas, em um único processador lógico (máscara de afinidade 1),
sem compilação concorrente:

| Carga própria | Interpretador VU1 | VU1 nativo | Resultado |
|---|---:|---:|---|
| Microprograma isolado | 5,828 milhões LIW/s | 6,841 milhões LIW/s | +17,397% de throughput |
| EE AOT + VU1 + GS, carga fria de 20.100 instruções EE | 1,941 ms | 2,2375 ms | tempo +15,276%; throughput −13,251% |

O ganho isolado não se repete nessa carga integrada curta. Uma hipótese é
que seleção, primeira validação da imagem e despacho não tenham sido
amortizados; esses custos não foram isolados por profiling. Amostras de
aproximadamente 2 ms também estão sujeitas a variação do host. A tabela
registra a regressão medida; nenhum desses números mede FPS comercial ou
desempenho de Shadow of the Colossus. Todas as variantes compararam o
mesmo estado completo e os pixels da fixture própria.

Geração em processo: 7,005 ms para 28 pares alcançáveis e 130.461 bytes
de C++ no benchmark. A execução da CLI, incluindo seu processo, levou
0,096725 s. O artefato de teste completo tem 29 pares (inclui a raiz de
wrap), 130.554 bytes de fonte; compilação MSVC Release `/O2 /Ob2` dessa
unidade: 3,065265 s e objeto de 417.877 bytes. O build sem mudanças do
harness particionado levou 0,165936 s. Esses custos não incluem um jogo
comercial nem estimam o custo de traduzir todos os seus microprogramas.

## Baseline EE comercial no núcleo final

Após congelar o núcleo, o harness comercial e todas as unidades EE AOT
dependentes dos cabeçalhos foram recompilados. A comparação final usou
cinco pares alternados do executável anterior e do novo, janela de
30.000.000 instruções, mesma afinidade 1, mesmos inputs e callbacks
RGBA/PCM; nenhuma compilação ou outro benchmark correu em paralelo.

| Medida nessa janela de menu | Executável anterior | Núcleo com infraestrutura VU1 |
|---|---:|---:|
| Tempos das cinco rodadas (s) | 11,7975; 12,2793; 11,5353; 11,0616; 11,1578 | 10,8593; 11,6688; 10,7865; 10,7873; 11,6115 |
| Tempo mediano | 11,5353 s | 10,8593 s |
| Intervalo mínimo–máximo | 11,0616–12,2793 s | 10,7865–11,6688 s |
| Throughput EE mediano | 2,600713 milhões/s | 2,762609 milhões/s |
| Intervalo de campo mediano observado | 1825,77 ms | 1760,55 ms |

Não se observou regressão mediana nessa janela: throughput EE +6,2251%,
com dispersão e intervalos sobrepostos. **Isso não é ganho VU1:** não houve
instrução VU1 nem programa nativo selecionado. Não atribuir essa variação
à aceleração vetorial ou extrapolar para gameplay/FPS comercial.

Os dez ensaios pareados e uma execução final com VU1 explicitamente
forçado ao interpretador preservaram, por comparação automatizada:

- mesmo estado completo, incluindo ciclos, interrupções e pipelines;
- seis campos RGBA de 640×224, nos mesmos ciclos e com os mesmos bytes;
- 9.766 amostras PCM com timestamps e valores idênticos;
- 25.446.236 instruções EE AOT e 4.553.764 interpretadas;
- zero instruções, verificações de identidade ou fallbacks VU1, confirmando
  a ausência dessa carga na janela disponível.

Fontes fixadas em `d6f0bd31d4108f4db982fbf8913013d812915539`;
esquema de checkpoint preservado `c649550347ab…`, runtime `366c8184e4e0…`.
SHA-256 do núcleo medido `0dc8eae5f0d1…`; executável final do harness
`03efe0ea61a1…`. Os hashes completos estão no registro de proveniência.
Logs, dados de comparação e proveniência ficam localmente em
`out/vu1-real-final-summary.json`. Fontes próprias do harness permanecem
ignoradas junto aos artefatos locais; nenhum firmware, microprograma
comercial, checkpoint ou trace privado foi publicado. Os ensaios e
processos de compilação terminaram; nenhuma sessão comercial ficou ativa.

## CI do código

Commit de código validado: `d6f0bd31d4108f4db982fbf8913013d812915539`.

- [Núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42124284459):
  **seis alvos aprovados**, Windows/Linux/macOS x64 e ARM64; **112 testes
  por alvo**, incluindo os dois novos testes VU1 e os builds Ninja.
- [Launcher](https://github.com/fabegalo/ps2-native/actions/runs/42124284542):
  **seis alvos aprovados**, sem alteração no launcher nesta tarefa.

Logs finais de Windows ARM64 e macOS Intel conferidos. A matriz de builds
não substitui a captura comercial VU1 ainda pendente. O acompanhamento da CI
terminou; nenhum observador ou ensaio desta tarefa ficou ativo.
