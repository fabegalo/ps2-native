# STATUS — ps2-native

**Registro vivo de progresso e evidências.** Atualizar este arquivo quando houver avanço **verificado**; não reescrever os READMEs, `AGENTS.md` ou `CLAUDE.md` a cada marco. Relatórios detalhados continuam em `docs/M*-STATUS.md`; o trabalho seguinte fica em `docs/NEXT-TASK.md`.

## Estado funcional verificado — 03/11/2026

**Protótipo experimental. Não executa o Browser original até o fim, não possui launcher público nem declara jogos comerciais jogáveis.**

| Componente | Evidência até este ponto | Limite |
|---|---|---|
| AOT MIPS/R5900 | ELF32 sintéticos, CFG conservador, relocations como evidência, geração C++ e testes end-to-end | Cobertura incompleta de jogos e código dinâmico |
| Boot EE | ROM0 reais, COP0/TLB, memória, RDRAM, início do kernel em RAM e AOT híbrido com trace | CPU e dispositivos ainda parciais |
| Direct ELF | Loader comum, ambiente bare-metal explícito, Count/Compare e retomada com overlays AOT verificados | Sem kernel Sony/HLE, syscalls convencionais ou suporte demonstrado a jogos |
| IOP | Inicialização R3000A, NVM externa, MECHACON, timers/VBLANK, SIF e EE→IOP SIF1 com bytes reais | Sem linker IRX, SIF0, RPC ou serviços completos; DMA/timing ainda parciais |
| GS/GIF | VRAM 4 MiB PSMCT32/24, PATH3 PACKED/REGLIST/IMAGE, transferências, pontos/sprites, scanout RGBA8/BMP e eventos; imagem sintética determinística | Sem pixels da BIOS real; formatos, primitivas, efeitos e timing ainda parciais |
| Launcher desktop | `ps2-native` (Qt 6 opcional): biblioteca persistente, identificação de BIOS/NVM pelo núcleo e de discos ISO/BIN/CUE, diagnóstico `ps2n-boot` assíncrono; janela real validada no Windows | Jogar e Browser bloqueados (sem runtime); sem release; diálogos e arrastar e soltar não automatizados |
| Distribuição | Builds/testes sintéticos nas seis arquiteturas de CI | Builds não comprovam funcionamento do firmware/jogo em cada host; sem release |

## Última evidência: incremento M3.0

- **Três ELF próprios pelo mesmo runtime:** chamadas/memória/interrupt de Count, comunicação SIF1 e frame
  VIF1 DIRECT→GIF PATH2→GS. Interpretador e AOT conferem memória, registradores, eventos e framebuffer.
- **SIF1 real dentro do modelo:** 256 bytes transferidos para a RAM IOP, checksum calculado no R3000A,
  backpressure e flags/IRQ conferidos; não é RPC/HLE nem prova de timing físico.
- **AOT genérico:** descoberta de funções por JALR constante, `--hybrid` para ELF e snapshot SHA-256 de
  região dinâmica para regeneração offline; overlay compilado confere bytes e invalida código modificado.
  No teste de CPU, interpretação caiu de 30.017 para 17 instruções e trocas de motor de 20.009 para 9.
- **Frame Direct ELF:** 64×32, digest RGBA8 `40231620…b6f436a3`, igual nos dois motores. Modo original
  de boot permanece separado; nenhum firmware ou jogo real foi reexecutado nesta etapa.
- **Escopo M3.0 ainda parcial:** IRX/linking, execução VU0/VU1 e INTC/serviços completos não foram
  implementados. O frame atual não atravessa VU. Evidências, medições, comandos e CI: [M3.0](docs/M3.0-STATUS.md).
- **Validação:** 36 verificações locais e CI do núcleo nos seis alvos aprovadas para `e80c6f2`;
  a workflow separada do launcher tem falhas de instalação Qt no Windows e de teste QML no macOS ARM64,
  registradas no relatório, sem alteração da GUI neste incremento.

## Evidência anterior: M2.9

- **Primeiro frame GS sintético:** 256×160, gerado por instruções R5900/GIF/GS; digest RGBA8 `43564b22…2e3f54`, igual no interpretador e no híbrido. API de framebuffer/eventos compartilhada e exportação BMP disponíveis, sem dependência gráfica obrigatória.
- **BIOS real repetida nos dois perfis SCPH-70004:** mantém 17.690.079 instruções EE, parada em `0x80007D20`; diagnóstico com IDs assumidos mantém 17.690.770 e parada em `0x8000E628`. Zero escritas de pixels em ambas as rotas; não houve avanço do Browser. As máscaras de IMR após reset foram estabelecidas pelo manual; fase vertical e INTC seguem pendentes.
- Relatório, comandos, contratos, testes e limites: [M2.9](docs/M2.9-STATUS.md). A evidência anterior de identificação de firmware permanece abaixo.

## Evidência anterior: M2.8

- **Firmware com NVM fornecidos localmente:** arquivo ROM0 SCPH-70004 (ROMVER `0200EC20040614`) e NVM Dragon de 1.024 bytes, compatíveis por identidade verificada; ambos **fora do Git**. Foram exercitados dois perfis possíveis de placa GH-032 que a NVM não permite distinguir.
- **Boot interpretado e híbrido:** 17.690.079 instruções EE e 2.210.789 IOP, traces equivalentes (desconsiderando marcadores do motor), com mensagens `Initialize Start.` e `Initialize GS ...`. A parada do EE é a leitura do ID `0x7E` do codificador de vídeo (I²C endereço `0x21`, controlador em `0x1A000000` na ROMVER 0200); o valor real não foi medido.
- **Diagnóstico separado, não boot comprovado:** assumir explicitamente o ID do codificador via opção de diagnóstico permitiu prosseguir até a leitura de `I_STAT` do EE após o envio de pacote GIF PATH3. **Não** utilizar esses resultados como confirmação de hardware.
- **IOP após reset do GS:** VBLANK fica não determinado até ser esclarecido o estado do `SYNCV`/PCRT; não inventar eventos nem interrupções.
- **Outras ROM0 reais** (SCPH-70012, SCPH-77008 e firmware japonês de 2006) continuam sem NVM correspondente validada; os resultados anteriores devem ser consultados em `docs/M2.7-STATUS.md`.
- **Testes:** a documentação M2.8 relata 25 testes CTest locais aprovados no Windows x86-64. CI do commit M2.8 deve ser conferida em [GitHub Actions](https://github.com/fabegalo/ps2-native/actions); a suíte remota só é aprovada quando todos os seis jobs terminarem com sucesso.

Detalhamento de endereços, perfis, SHA-256, suposições, componentes e bloqueios: [M2.8](docs/M2.8-STATUS.md).

## Launcher desktop (fundação da interface)

- **Executável `ps2-native`** (Qt 6 Quick, LGPL-3.0 dinâmico; `PS2N_LAUNCHER=AUTO|ON|OFF`): Início, Biblioteca, BIOS, Configurações e Sobre. Gerencia o conteúdo do usuário e **não substitui** Browser, System Configuration nem boot de disco; ações sem runtime aparecem bloqueadas com o motivo.
- **Núcleo compartilhado `ps2n_frontend`**, sem GUI: BIOS por `load_bios_rom0`, NVM por `load_nvm_image` contra o perfil da mesma ROMVER, discos ISO 9660 (2048/2352 bytes, Mode 1/2), CUE multifaixa, leitura por faixa acima de 4 GiB; biblioteca versionada com escrita atômica, sem copiar ISOs ou firmware; contratos futuros do runtime (`RuntimeSession`, renderizadores, controle, áudio) declarados e indisponíveis.
- **Testes:** `launcher-core` (headless, também na CI do núcleo) e `launcher-ui-import`/`launcher-ui-restore` (janela real offscreen: importação, páginas, diagnóstico, persistência e restauração) aprovados no Windows x86-64 com Qt 6.8.3, sem avisos de QML. Novo workflow `launcher.yml` para os seis alvos (Qt 6.8.3 oficial no Windows, Qt 6.4 do Ubuntu, Qt do Homebrew); conferir o resultado por commit no GitHub Actions.
- **Validação gráfica local** (plataforma `windows`): todas as páginas capturadas em 1360×860, 1600×1000 e 1000×660; pasta local com 60 ROM0 reais validadas, uma ISO de DVD de PS2 e um CD de PlayStation em CUE/BIN identificados; diagnóstico numa ROM0 real até o limite de instruções. Conteúdo local fora do Git.
- Limites: jogos, Browser, controle, áudio e renderização dependem do runtime; imagens comprimidas não são lidas; diálogos nativos e arrastar e soltar não foram exercitados automaticamente. Contratos e detalhes: [LAUNCHER.md](docs/LAUNCHER.md).

## I/O do host: caminhos longos e Unicode

- Leitura de BIOS/NVM/ELF, escrita de AOT/traces/fixtures, diretórios e verificações centralizados em `src/io.cpp`. Argumentos UTF-16 preservados desde `wmain`; sem mudanças no Registro ou cópias para contornar limites de caminho.
- Regressão permanente `io-long-paths`: 260, 500 e 1.000 unidades nativas, caminhos relativos/absolutos/estendidos, Unicode, parâmetros FILEPATH do CMake, escrita idempotente e recusas explícitas.
- Windows x86-64/MSVC 19.44: os 25 testes existentes e o novo teste de I/O passaram. UNC real aprovado num compartilhamento local já existente, com caminhos de 277, 511 e 1.019 unidades UTF-16. A CI sem compartilhamento informa explicitamente que UNC real não foi validado.
- Compilação AOT profunda corrigida: o CMake referencia o diretório original por uma junction local ao build, verifica identidade do arquivo e conserva as dependências do Ninja, sem copiar o fonte ou alterar Registro. O runner com fonte Unicode de 983 unidades compilou; a regressão `aot-long-path-build` aprovou compilação/execução, no-op e rebuilds por mudanças no fonte/header em 260/500/1.000 unidades no Windows x86-64. A captura exata do prefixo localizado de `/showIncludes` também foi corrigida. O adaptador requer alvo local e filesystem com junctions; detalhes e limites no relatório de caminhos.

Auditoria, reprodução e limites: [caminhos do host](docs/WINDOWS-PATHS.md). A validação dos seis alvos é acompanhada por commit no [GitHub Actions](https://github.com/fabegalo/ps2-native/actions).

## Pendências principais

1. Obter evidência real dos IDs do codificador de vídeo e do estado GS/IMR/CSR após reset, sem respostas inventadas.
2. Reproduzir VBLANK/INTC EE e manter o sincronismo EE↔IOP após reconfiguração do GS.
3. Validar e implementar a comunicação SIF e DMA com transferência de bytes real e tags/FIFO corretos.
4. Avançar o boot do firmware original até o Browser, sem falsificar dados NVM, periféricos ou compatibilidade.
5. Ampliar o caminho AOT genérico e, depois, os modelos GS/VU/VIF/SPU2, medindo correção e desempenho.

Ver [próxima tarefa](docs/NEXT-TASK.md). Esta lista é dinâmica; não deve ser duplicada no README.

## Histórico de marcos (detalhes nos relatórios)

| Marco | Principal evidência |
|---|---|
| M0–M1.3 | Fundação ELF32, código nativo sintético, indiretos, relocations e semântica R5900 parcial |
| M2.0–M2.2 | Loader ROM0, execução real até a configuração GS/RDRAM, temporização inicial e SIO |
| M2.3–M2.5 | Protocolo MCH/RDRAM, perfis de placa e progresso da BIOS até a memória do IOP |
| M2.6 | Carregamento da NVM do MECHACON, boot IOP, timers e exceções |
| M2.7 | VBLANK, registradores SIF e sincronização dos acessos EE/IOP |
| M2.8 | Primeira ROM0/NVM real pareada; codificador de vídeo e caminho GIF PATH3 |

Revisar a evidência original e os limites de cada etapa nos relatórios [docs](docs/README.md). Não interpretar o número de instruções retiradas como certificação de boot, vídeo ou jogos.
