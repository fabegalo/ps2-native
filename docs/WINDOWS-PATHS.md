# Caminhos do host e limites externos

A fronteira de I/O do projeto e `include/ps2native/io.hpp` / `src/io.cpp`.
BIOS, NVM e ELF entram por `read_file`. AOT e traces usam
`write_text_if_changed`; fixtures sinteticos usam `write_binary_if_changed`.
Criacao de diretorios, existencia, timestamps e limpeza dos testes tambem
passam por essa camada. Nao ha outro fstream nem conversao `.string()` de
argumentos nas ferramentas/testes.

No Windows, `wmain` conserva caminhos UTF-16. Opcoes textuais/diagnosticos usam
UTF-8; os valores de `--nvm=` e `--trace-out=` continuam sendo caminhos nativos.
Caminhos comuns sao resolvidos com `GetFullPathNameW` dimensionado dinamicamente
e recebem `\\?\` (drive) ou `\\?\UNC\` (rede). Caminhos ja estendidos sao
literais e preservados. Cada operacao resolve o caminho uma vez e reutiliza o
resultado para comparar, criar diretorios e escrever. Nenhuma configuracao do
Registro, manifest opt-in, normalizacao Unicode ou copia de entrada e exigida.
Linux/macOS usam a mesma implementacao compartilhada, sem o prefixo Win32.

Escritas de texto/binario com conteudo identico retornam `false` sem abrir o
arquivo para escrita. A comparacao usa blocos de 64 KiB; nao carrega outra copia
inteira da saida. Arquivos diferentes sao escritos e fechados com verificacao
de erro. O limite de leitura existente de 128 MiB continua independente do
comprimento do caminho.

## Protecao permanente

`io-long-paths` gera exclusivamente firmware/NVM/ELF sinteticos. Exercita
caminhos absolutos de 260, 500 e 1000 unidades nativas (UTF-16 no Windows),
argumentos relativos, acentos, japones, emoji, espacos e pontuacao valida.
Valida os loaders, as ferramentas reais `ps2n-aot`, `ps2n-boot` e
`ps2n-make-fixture`, a exportacao completa de traces, diretorios ausentes,
conteudo alterado/igual/vazio e timestamps preservados. Tambem passa caminhos
Unicode longos como parametros FILEPATH reais do CMake a `tests/io_cli.cmake`,
que encaminha os argumentos a ferramenta sem implementar I/O paralelo.
O teste usa processos sem shell, com timeout, e limpa apenas sua arvore propria.

Casos negativos: caminho vazio, NUL embutido, componente de 300 caracteres,
entrada ausente, arquivo usado como diretorio e diretorio usado como arquivo.
No Windows cobre tambem o limite total Win32, barras normais, `.`/`..`, ausencia
de prefixos duplicados e preservacao literal de caminhos estendidos.

UNC: a conversao normal/estendida e testada sem acesso a rede. Para testar I/O
real, configurar `-DPS2N_TEST_UNC_ROOT=\\servidor\compartilhamento` com um
compartilhamento gravavel ja existente. Se fornecido, erros de acesso falham o
teste; sem ele, o teste imprime explicitamente `UNC real network I/O NOT
VALIDATED`. Nenhum compartilhamento e criado e nenhuma credencial e alterada.

Validacao local em 2026-11-03: Windows x86-64, MSVC 19.44, CMake 3.31.6;
`io-long-paths` aprovado nos tres comprimentos, inclusive FILEPATH via CMake.
Os 25 testes da base M2.8 passaram no build isolado (26 com o novo teste de I/O). O teste de I/O foi incluido
na matriz CI de seis alvos, com uma etapa verbosa propria (Windows x86-64/ARM64,
Linux x86-64/ARM64 e macOS Intel/Apple Silicon).
I/O UNC real aprovado localmente no compartilhamento administrativo ja existente de localhost, restrito a out: caminhos de 277, 511 e 1019 unidades UTF-16, nomes Unicode, leitura normal/estendida, escrita idempotente e limpeza. Nenhum compartilhamento, servico, credencial ou Registro foi alterado. A CI sem compartilhamento continua registrando a cobertura UNC real como nao validada.

## Limites reais e ferramentas externas

- O prefixo estendido nao elimina os limites por componente do sistema de
  arquivos (comumente 255), o limite total Win32 (~32767 UTF-16 incluindo NUL),
  permissoes de acesso, limites do servidor SMB ou limites do sistema Unix.
  Falhas indicam operacao, caminho e motivo do sistema quando disponivel.
- CMake conserva os argumentos de entrada e saida entre aspas (`VERBATIM` nas
  regras); a criacao de diretorios de fixtures foi retirada de `cmake -E` e
  pertence agora ao escritor compartilhado. A raiz do build e as operacoes
  internas de CMake/compilador continuam sendo responsabilidade dessas ferramentas.
## Compilacao AOT pelo Ninja sem copias

A falha original foi reproduzida: Ninja 1.13.1 recusava `Stat` de uma fonte
AOT Unicode com 983 unidades UTF-16, apesar de o CMake 3.31.6 a configurar.
A integracao `cmake/AotSource.cmake` agora conserva a opcao publica
`PS2N_BIOS_AOT_SOURCE` e referencia automaticamente o diretorio original por
um caminho curto dentro do build quando necessario no Windows.

`ps2n-path`, compilado no configure apenas com `src/io.cpp` e a fronteira de
I/O publica, cria uma junction por APIs Win32. O buffer de reparse usa caminhos
NT dimensionados, pois o probe com `mklink /J` e alvo profundo produziu uma
referencia invalida. Nenhum fonte ou firmware e copiado; apenas o executavel
proprio do helper e instalado no build pelo `try_compile`. Os arquivos AOT,
ELF e headers conservam sua localizacao original. O helper verifica identidade
de arquivo, recusa caminhos existentes que nao sejam referencias ao mesmo
alvo e nao retargeta aliases. Nao usa hard links, wrapper C++, Registro,
Developer Mode ou privilegio de criacao de symlink. A referencia e local ao
build; nao constitui cache compartilhado de firmware.

O Ninja recebe o caminho curto do mesmo arquivo e continua gerenciando suas
dependencias normalmente. Uma segunda falha foi demonstrada no MSVC 19.44
localizado: o CMake decodificava `/showIncludes` UTF-8 como OEM e o Ninja nao
reconhecia headers alterados. O modulo captura o prefixo real sem transcodificar
seus bytes. `/utf-8` torna o charset C++ explicito e atualiza comandos de
compilacao antigos, para renovar seus registros de dependencias.

A regressao permanente `aot-long-path-build` cria somente fixtures proprios,
compila o C++ AOT original sob Ninja em 260/500/1000 unidades nativas e executa
37 instrucoes MIPS com registradores/RAM/terminacao e limites conferidos.
Compara identidade do arquivo original/referencia, exige no-op sem alteracoes,
rebuild apos editar o fonte original e apos editar um header original,
e comprova que remover a referencia preserva os dados. Cobre aliases relativos,
reuso idempotente, conflito com diretorio comum/outro alvo e recusas explicitas.
Os processos de teste e seus filhos tem timeout e grupo/job proprio.

Validacao local: Windows x86-64, MSVC 19.44 localizado, CMake 3.31.6 e Ninja
1.13.1; os tres casos exatos passaram em 52,38 s. O runner opcional tambem foi
compilado automaticamente com o parametro original de 983 unidades, sem
informar um alias manual. A CI exige que Ninja esteja disponivel para essa
regressao nos seis alvos. Fora da CI, a ausencia de Ninja e reportada como
cobertura nao validada (teste skipped), sem bloquear builds por geradores nativos.

Limites restantes: a raiz do build e o basename precisam permitir um caminho
curto para as ferramentas nativas; isso nao promete builds de projetos inteiros
em raizes arbitrariamente profundas. O adaptador Windows requer filesystem com
suporte a junctions e alvo local; junctions para UNC/rede sao recusadas antes
da criacao. O I/O normal/UNC dos dados continua disponivel como descrito acima.
Limites de reparse point, filesystem, permissoes, SDK e toolchain continuam
produzindo erros explicitos. O bootstrap usa o compilador nativo selecionado;
cross-compilacao com binarios de host nao executaveis nao foi validada.

Referencia de sintaxe/limites: [Microsoft: Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation).
