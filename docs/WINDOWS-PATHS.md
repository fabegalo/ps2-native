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
I/O UNC real nao validado localmente: nenhum compartilhamento foi fornecido.

## Limites reais e ferramentas externas

- O prefixo estendido nao elimina os limites por componente do sistema de
  arquivos (comumente 255), o limite total Win32 (~32767 UTF-16 incluindo NUL),
  permissoes de acesso, limites do servidor SMB ou limites do sistema Unix.
  Falhas indicam operacao, caminho e motivo do sistema quando disponivel.
- CMake conserva os argumentos de entrada e saida entre aspas (`VERBATIM` nas
  regras); a criacao de diretorios de fixtures foi retirada de `cmake -E` e
  pertence agora ao escritor compartilhado. A raiz do build e as operacoes
  internas de CMake/compilador continuam sendo responsabilidade dessas ferramentas.
- Limite externo reproduzido, sem alterar Registro: CMake 3.31.6 configurou
  `PS2N_BIOS_AOT_SOURCE` para uma traducao sintetica com nome Unicode em caminho
  de 983 unidades UTF-16. A criacao da ROM0 e do C++ pelas ferramentas do projeto
  funcionou. O Ninja 1.13.1 recusou o build antes da compilacao com
  `Stat(...): Filename longer than 260 characters`. Isso limita esse toolchain
  para fontes C++ profundas, nao as leituras/escritas do ps2-native. Nenhum arquivo
  foi copiado para encobrir a falha. Nao se afirma suporte irrestrito a raizes
  de projeto/build profundas no Ninja/MSBuild/compilador.

Referencia de sintaxe/limites: [Microsoft: Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation).
