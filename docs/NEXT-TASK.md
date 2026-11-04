# Próximo trabalho — The Simpsons: Hit & Run (SLUS-20624)

Meta: inicialização normal → primeiro frame real → menu original → controles → gameplay.
Só a execução inicial do ELF foi demonstrada; zero pixels reais. Hashes e limites no
[FIRST-GAME-STATUS](FIRST-GAME-STATUS.md).

1. Completar o multiplicador Booth geral do EE, bloqueio diagnóstico em `0x002DEAF4`.
   Estudar primeiro PS2Recomp e confrontar com especificações/resultados independentes;
   a multiplicação do host na referência não prova o arredondamento R5900.
   O subconjunto esparso atual deve continuar exato; casos restantes falham explicitamente.
2. Repetir o ELF com o kernel Sony original. A injeção local após BIOS parcial chegou a
   627.894 instruções, com RAM idêntica em interpretador/híbrido; isso não certifica ExecPS2.
   O boot original continua dependente dos IDs físicos indisponíveis do encoder GH-032.
   O mantenedor já informou que não possui medição: não repetir a solicitação nem inventar IDs.
3. Seguir os acessos reais de IOP/SIF/DMA, usando os onze módulos/IOPRP254 do próprio disco,
   sem HLE substituto, mistura de firmware ou respostas de sucesso simuladas.
4. Manter AOT apenas para código identificado e fallback explícito para regiões dinâmicas.
   Os 214.944 passos AOT atuais são majoritariamente limpeza de RAM, não cobertura do jogo.
   Avançar VIF/VU/GS, input e SPU2 conforme exigidos; promover etapas somente com evidência.

Regressões focadas, equivalência AOT/interpretador e CI dos seis alvos. Preservar o launcher,
READMEs, contratos, ISO, firmware, NVM e saves. Traces, extrações, snapshots e traduções
proprietárias permanecem somente em `out/` ignorado.
