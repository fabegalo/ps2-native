# STATUS — ps2-native

**Registro vivo de progresso e evidências.** Atualizar este arquivo quando houver avanço **verificado**; não reescrever os READMEs, `AGENTS.md` ou `CLAUDE.md` a cada marco. Relatórios detalhados continuam em `docs/M*-STATUS.md`; o trabalho seguinte fica em `docs/NEXT-TASK.md`.

## Estado funcional verificado — 03/11/2026

**Protótipo experimental. Não executa o Browser original até o fim, não possui launcher público nem declara jogos comerciais jogáveis.**

| Componente | Evidência até este ponto | Limite |
|---|---|---|
| AOT MIPS/R5900 | ELF32 sintéticos, CFG conservador, relocations como evidência, geração C++ e testes end-to-end | Cobertura incompleta de jogos e código dinâmico |
| Boot EE | ROM0 reais, COP0/TLB, memória, RDRAM, início do kernel em RAM e AOT híbrido com trace | CPU e dispositivos ainda parciais |
| IOP | Inicialização R3000A, NVM externa, MECHACON, timers/VBLANK e registradores SIF compartilhados | Sem serviços completos, DMA SIF ou interrupções de hardware completas |
| GS/GIF | Registradores de controle, timing e pacotes limitados GIF PATH3 PACKED A+D | Sem memória local, framebuffer, imagem de jogo ou renderização |
| Distribuição | Builds/testes sintéticos nas seis arquiteturas de CI | Builds não comprovam funcionamento do firmware/jogo em cada host; sem release |

## Última evidência: M2.8

- **Firmware com NVM fornecidos localmente:** arquivo ROM0 SCPH-70004 (ROMVER `0200EC20040614`) e NVM Dragon de 1.024 bytes, compatíveis por identidade verificada; ambos **fora do Git**. Foram exercitados dois perfis possíveis de placa GH-032 que a NVM não permite distinguir.
- **Boot interpretado e híbrido:** 17.690.079 instruções EE e 2.210.789 IOP, traces equivalentes (desconsiderando marcadores do motor), com mensagens `Initialize Start.` e `Initialize GS ...`. A parada do EE é a leitura do ID `0x7E` do codificador de vídeo (I²C endereço `0x21`, controlador em `0x1A000000` na ROMVER 0200); o valor real não foi medido.
- **Diagnóstico separado, não boot comprovado:** assumir explicitamente o ID do codificador via opção de diagnóstico permitiu prosseguir até a leitura de `I_STAT` do EE após o envio de pacote GIF PATH3. **Não** utilizar esses resultados como confirmação de hardware.
- **IOP após reset do GS:** VBLANK fica não determinado até ser esclarecido o estado do `SYNCV`/PCRT; não inventar eventos nem interrupções.
- **Outras ROM0 reais** (SCPH-70012, SCPH-77008 e firmware japonês de 2006) continuam sem NVM correspondente validada; os resultados anteriores devem ser consultados em `docs/M2.7-STATUS.md`.
- **Testes:** a documentação M2.8 relata 25 testes CTest locais aprovados no Windows x86-64. CI do commit M2.8 deve ser conferida em [GitHub Actions](https://github.com/fabegalo/ps2-native/actions); a suíte remota só é aprovada quando todos os seis jobs terminarem com sucesso.

Detalhamento de endereços, perfis, SHA-256, suposições, componentes e bloqueios: [M2.8](docs/M2.8-STATUS.md).

## I/O do host: caminhos longos e Unicode

- Leitura de BIOS/NVM/ELF, escrita de AOT/traces/fixtures, diretórios e verificações centralizados em `src/io.cpp`. Argumentos UTF-16 preservados desde `wmain`; sem mudanças no Registro ou cópias para contornar limites de caminho.
- Regressão permanente `io-long-paths`: 260, 500 e 1.000 unidades nativas, caminhos relativos/absolutos/estendidos, Unicode, parâmetros FILEPATH do CMake, escrita idempotente e recusas explícitas.
- Windows x86-64/MSVC 19.44: os 25 testes existentes e o novo teste de I/O passaram. UNC real aprovado num compartilhamento local já existente, com caminhos de 277, 511 e 1.019 unidades UTF-16. A CI sem compartilhamento informa explicitamente que UNC real não foi validado.
- Limite externo reproduzido: as ferramentas do projeto geram AOT num caminho Unicode de 983 unidades e o CMake 3.31.6 configura o runner, mas o Ninja 1.13.1 recusa o `Stat` da fonte antes da compilação por exceder 260 caracteres. Nenhum workaround por cópia ou Registro foi aplicado.

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
