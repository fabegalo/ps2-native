# Decisão de licença — 2026-11-03

**Decisão para o M0:** BSD-3-Clause para código original independente (`LICENSE`). Nenhum código de GPL foi copiado, ligado, distribuído ou incorporado. Nenhuma dependência C++ externa foi adicionada. A matriz CI usa `actions/checkout` fixado no SHA da versão v4.2.2.

| Projeto consultado | Arquivos oficiais verificados | Licença declarada | Uso no M0 |
|---|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | `LICENSE`, `ps2xRecomp/src/lib/elf_parser.cpp`, `control_flow_analyzer.cpp`, `vu_translator.cpp` | GPL-3.0 | consulta arquitetural, sem integração |
| [PCSX2](https://github.com/PCSX2/pcsx2) | `pcsx2/Docs/License.txt`, `pcsx2/Interpreter.cpp`, `pcsx2/Memory.cpp` | GPL-3.0 ou posterior | referência comportamental, sem integração |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | repositório/licença | MIT | referência de conceito, sem código |
| [Play!](https://github.com/jpd002/Play-) | `License.txt`, `README.md` | licença permissiva de 2 cláusulas | referência arquitetural, sem código |

Código GPL incorporado/derivado/ligado ao programa pode impor requisitos GPL sobre o trabalho resultante e obrigações de publicação do código correspondente, incompatíveis com apresentá-lo puramente como BSD. Antes de qualquer integração, decidir explicitamente: adotar GPL quando necessário, obter autorização compatível, ou usar uma implementação original independente. Distribuição separada de executáveis possui nuances jurídicas e não é uma exceção universal ao copyleft; **não presumir compatibilidade**. Revisão jurídica externa é apropriada antes de distribuição comercial.

Todos os artefatos AOT derivados de jogos continuam sujeitos aos direitos sobre o código de entrada. Não distribuir jogos, imagens, ELF, BIOS, fontes, logos, boot animations, assets proprietários ou traduzir e publicar código do usuário sem permissão.

Fontes consultadas em 03/11/2026:
- https://github.com/ran-j/PS2Recomp/blob/main/LICENSE
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/License.txt
- https://github.com/N64Recomp/N64Recomp/blob/main/LICENSE
- https://github.com/jpd002/Play-/blob/master/License.txt
