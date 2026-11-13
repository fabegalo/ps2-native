<div align="center">

<img src="docs/assets/ps2-native-banner.webp" alt="ps2-native â€” banner de recompilaÃ§Ã£o nativa do PlayStation 2" width="100%" />

# ps2-native

**Projeto aberto de pesquisa para executar software original do PlayStation 2 por recompilaÃ§Ã£o nativa antecipada (AOT).**

[![CI](https://img.shields.io/github/actions/workflow/status/fabegalo/ps2-native/build.yml?branch=main&label=CI)](https://github.com/fabegalo/ps2-native/actions/workflows/build.yml)
[![Experimental](https://img.shields.io/badge/estado-experimental-orange)](STATUS.md)
[![LicenÃ§a](https://img.shields.io/badge/licen%C3%A7a-BSD--3--Clause%20%2B%20GPL--3.0-blue)](docs/LICENSE-DECISION.md)

[English](README.md) Â· [PortuguÃªs (Brasil)](README.pt-BR.md) Â· [Estado atual](STATUS.md) Â· [DocumentaÃ§Ã£o](docs/README.md) Â· [Como contribuir](CONTRIBUTING.md)

</div>

> **Software experimental de pesquisa, nÃ£o um launcher de PS2 jogÃ¡vel.** O projeto analisa e recompila um subconjunto verificado de cÃ³digo do PS2 e executa parte do firmware original num runtime AOT/nativo offline que recusa explicitamente traduÃ§Ãµes ausentes. Ainda **nÃ£o** completa o boot do Browser original nem executa jogos comerciais. CI aprovada nÃ£o comprova jogabilidade ou compatibilidade funcional em cada plataforma.

## O que Ã© o ps2-native?

O objetivo Ã© executar a **BIOS/interface e os jogos originais do PlayStation 2** em computadores modernos, seguindo o modelo do x360-native: cÃ³digo original traduzido estaticamente (**AOT**) e serviÃ§os de sistema implementados por **HLE nativo**. InstruÃ§Ãµes de EE/R5900, IOP/R3000A e VU0/VU1 que executarem devem estar compiladas para o host. GS, DMA, SPU2, controles, memÃ³ria e temporizaÃ§Ã£o continuam necessÃ¡rios no runtime compartilhado.

Interpretadores sÃ£o restritos a binÃ¡rios de validaÃ§Ã£o, fora dos executÃ¡veis de produÃ§Ã£o. MÃ³dulos dinÃ¢micos exigem preparaÃ§Ã£o AOT offline; HLE pode fornecer serviÃ§os de BIOS/kernel e IOP sem interpretar instruÃ§Ãµes. Isso nÃ£o autoriza recriar as telas da BIOS ou ignorar efeitos convidados. Esta Ã© a polÃ­tica de execuÃ§Ã£o exigida; nÃ£o Ã© uma afirmaÃ§Ã£o de implementaÃ§Ã£o completa ou gameplay validado.

O **Browser original do PlayStation 2** e as telas de configuraÃ§Ã£o do sistema sÃ£o alvos prÃ³prios. O launcher desktop de gerenciamento oferece **Importar â†’ Preparar â†’ Jogar** por mÃ³dulos nativos verificados, usando a toolchain local configurada. TraduÃ§Ãµes e serviÃ§os ausentes param explicitamente; o launcher nÃ£o substitui os menus da Sony por imitaÃ§Ãµes.

## Estado atual

| Subsistema | Demonstrado | Ainda falta |
|---|---|---|
| **AOT nativo** | AnÃ¡lise ELF32/MIPS validada, descoberta conservadora de fluxo, geraÃ§Ã£o de C++ e testes sintÃ©ticos de ponta a ponta | TraduÃ§Ã£o abrangente de jogos comerciais e cÃ³digo dinÃ¢mico |
| **Emotion Engine** | SemÃ¢ntica parcial R5900, memÃ³ria/TLB, boot de hardware e execuÃ§Ã£o AOT/nativa offline | CPU completa, temporizaÃ§Ã£o e exceÃ§Ãµes precisas |
| **IOP e firmware** | Boot parcial R3000A, registradores de hardware, NVM externa, flags SIF e VBLANK | Boot completo da BIOS/Browser, interrupÃ§Ãµes, DMA SIF e serviÃ§os restantes |
| **GrÃ¡ficos** | Controle GS/GIF, temporizaÃ§Ã£o e um caminho limitado de pacotes GIF PATH3 | RenderizaÃ§Ã£o de framebuffer, GS/VU/VIF completos e grÃ¡ficos de jogos |
| **Jogos e experiÃªncia** | Biblioteca desktop, preparaÃ§Ã£o de mÃ³dulos nativos, ferramentas tÃ©cnicas e fixtures sintÃ©ticos | Jogos jogÃ¡veis, instaladores e Releases pÃºblicas |

**O registro atualizado e baseado em evidÃªncias fica em [STATUS.md](STATUS.md).** Ele separa execuÃ§Ãµes de firmware real, hipÃ³teses de diagnÃ³stico, resultados da CI e funcionalidades ausentes. Os detalhes de cada marco ficam nos [relatÃ³rios tÃ©cnicos](docs/README.md), sem serem copiados para este README a cada mudanÃ§a.

## Como funciona

1. Carregar e validar firmware fornecido pelo usuÃ¡rio ou um executÃ¡vel MIPS obtido legalmente. Isolar cada revisÃ£o/regiÃ£o de BIOS e seus recursos correspondentes.
2. Analisar cÃ³digo comprovadamente alcanÃ§Ã¡vel e gerar blocos AOT em C++20 somente com semÃ¢ntica suportada.
3. Executar os artefatos AOT preparados com os serviÃ§os HLE nativos necessÃ¡rios. CÃ³digo ausente exige preparaÃ§Ã£o offline e diagnÃ³stico preciso; nÃ£o hÃ¡ fallback interpretado permitido no produto.
4. Melhorar a fidelidade ao hardware com evidÃªncias tÃ©cnicas, testes sintÃ©ticos reproduzÃ­veis e observaÃ§Ãµes documentadas de execuÃ§Ãµes de firmware real. OperaÃ§Ãµes nÃ£o implementadas nunca devem retornar sucesso silencioso.

**Performance-first, correctness-preserving:** otimizar recompilador, CPU, memÃ³ria e grÃ¡ficos preservando o comportamento do console. Um futuro modo grÃ¡fico aprimorado deverÃ¡ preservar lÃ³gica, fÃ­sica e sincronizaÃ§Ã£o dos jogos.

## Compilar e testar

Requisitos: **CMake 3.21+**, **compilador C++20** e **Ninja** (ou gerador nativo compatÃ­vel). NÃ£o hÃ¡ SDK externo obrigatÃ³rio nem exigÃªncia de instruÃ§Ãµes AVX.

```sh
git submodule update --init external/ps2recomp
git -C external/ps2recomp apply ../../patches/ps2recomp/*.patch
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_RELEASE_LTCG=OFF
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

No Windows, tambÃ©m Ã© possÃ­vel usar o gerador Visual Studio do CMake; em builds com vÃ¡rias configuraÃ§Ãµes, usar `--config Release` para compilar e testar.

O GitHub Actions compila e testa **Windows x86-64/ARM64, Linux x86-64/ARM64 (incluindo a arquitetura do Steam Deck) e macOS Intel/Apple Silicon**. Compilar nos seis alvos nÃ£o comprova que os jogos de PS2 funcionam neles.

### Ferramentas de desenvolvimento

A execução usa o submódulo fixado do [PS2Recomp](https://github.com/ran-j/PS2Recomp), com patches versionados autenticados por `deps.lock.json`. No Windows, `scripts/apply-sdk-patches.ps1` aplica os patches de forma idempotente (`-Check` verifica, `-Revert` restaura o pin). Release sem LTCG evita links muito demorados no MSVC para jogos grandes; as demais flags Release do upstream são preservadas.

```sh
# Extrair um disco obtido legalmente, recompilar com o PS2Recomp, compilar e executar.
./out/build/ps2n play seu-disco.cue
```

`ps2n play` mantém cada jogo preparado em `out/build/games/<id>` e recompila automaticamente quando o runtime reporta um entry point ausente. A primeira preparação leva vários minutos; a compatibilidade depende do PS2Recomp e do jogo.

Caminho experimental de firmware: `ps2n prepare-bios <ROM0> <perfil-do-console> <NVM>` prepara o ELF OSDSYS original, validando revisão/perfil/NVM; `ps2n bios` com os mesmos argumentos também o inicia. Para payloads compactados suportados, a primeira execução captura e verifica a saída do loader original, prepara o módulo OSDSYS descompactado por hash e o relança automaticamente; as seguintes o reutilizam. A revisão validada entra no programa principal original, no mesmo runtime, e aguarda a conclusão do reinício IOP; isso ainda **não** completa o boot do Browser. Os originais permanecem somente para leitura.

ROM0, ROM1/ROM2, NVM, jogos e traduÃ§Ãµes proprietÃ¡rias geradas **nÃ£o sÃ£o fornecidos**. Mantenha esses arquivos localmente e nÃ£o os publique no repositÃ³rio. A correspondÃªncia BIOS/NVM deve ser validada, nÃ£o presumida pelo nome.

## Roadmap

A direÃ§Ã£o Ã© **AOT genÃ©rico de EE/IOP/VU + serviÃ§os HLE nativos â†’ BIOS/Browser original e jogos, com GS/DMA/SPU2, controles e temporizaÃ§Ã£o fiÃ©is â†’ launcher multiplataforma e velocidade original medida**. BIOS e jogos sÃ£o alvos obrigatÃ³rios; concluir o boot de hardware nÃ£o Ã© prÃ©-requisito para avanÃ§ar no caminho de um jogo. Isso nÃ£o Ã© uma promessa de compatibilidade nem um cronograma de lanÃ§amento. Acompanhe a [prÃ³xima tarefa de engenharia](docs/NEXT-TASK.md) para o trabalho ativo.

## ContribuiÃ§Ã£o e documentaÃ§Ã£o

ContribuiÃ§Ãµes devem incluir testes focados e evidÃªncias, manter um nÃºcleo Ãºnico em C++20 para todas as plataformas e evitar hacks por jogo, fallbacks silenciosos e cÃ³pias desnecessÃ¡rias. Consulte [CONTRIBUTING.md](CONTRIBUTING.md), [arquitetura](docs/ARCHITECTURE.md), [documentaÃ§Ã£o tÃ©cnica](docs/README.md) e [status](STATUS.md).

## LicenÃ§a

O cÃ³digo original conserva **[BSD-3-Clause](LICENSE)**. O [kernel HLE compilado diretamente do PS2Recomp](licenses/PS2Recomp/NOTICE) usa **[GPL-3.0](licenses/PS2Recomp/LICENSE)**; binÃ¡rios combinados devem cumprir a GPL-3.0, incluindo o cÃ³digo-fonte correspondente. PCSX2 continua como referÃªncia de comportamento. Firmware, jogos, NVM especÃ­fica de aparelho e traduÃ§Ãµes proprietÃ¡rias nÃ£o estÃ£o incluÃ­dos. Veja as [decisÃµes de licenciamento](docs/LICENSE-DECISION.md).
