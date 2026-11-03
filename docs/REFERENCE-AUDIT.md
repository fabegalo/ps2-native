# Referências técnicas versus requisitos do ps2-native — 03/11/2026

> Observações baseadas no README e em arquivos específicos dos repositórios consultados. Não são prova de que todo código desses projetos funciona em jogos reais. **Nenhuma dependência de código foi integrada.**

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
