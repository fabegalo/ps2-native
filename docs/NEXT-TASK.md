# Próximo trabalho — continuar Shadow of the Colossus (SCUS-97472)

Meta preservada: inicialização normal → primeiro frame real → menu original → controles → gameplay.
Só "ELF executa" está demonstrado. Ver [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md) para os hashes,
checkpoints e a distinção entre Direct ELF, boot normal e injeção diagnóstica em firmware parcial.

1. **Dependência externa do boot original:** obter identificação medida do codificador de vídeo
   (`0x7E`/eventual `0x7F`, endereço serial `0x42`) do console compatível com ROMVER
   `0200EC20040614`/NVM SCPH-70004. A ROM0/NVM local não contém esses dados. Repetir o boot com
   os valores comprovados; nenhum valor assumido ou fallback de outra revisão certifica a etapa.
2. **Timer/INTC exigido pelo mesmo ELF:** implementar contagem/compare/overflow, flags W1C e
   interrupções de T0–T3 com I_STAT/I_MASK e entrega INT0 no dispatcher comum. Último ponto
   diagnóstico: escrita `T2_MODE=0xF82`, PC `0x001103E8`, após 312.702 instruções. Conferir COMP
   e estado efetivamente programados; reset desconhecido não pode virar zero inventado. Testar
   bordas, máscaras, rearmamento, atraso de branch, EXL/ERL e equivalência AOT/interpretador.
3. Retomar o jogo **pelo kernel realmente inicializado**, sem apresentar a injeção no estado
   interrompido como ExecPS2. Seguir o primeiro bloqueio observado de SIF/DMA/IOP; os IRX e
   IOPRP300 devem vir do próprio disco, sem mistura de módulos de firmware nem HLE de sucesso.
4. Preservar código estático provado em AOT; o bootstrap tem só 23 instruções traduzidas,
   e a execução inicial quase toda AOT é limpeza de RAM, não cobertura dos XFF dinâmicos.
   Usar prova independente e regeneração offline para regiões carregadas; não adivinhar funções.
5. Avançar VIF/VU/GS, input e SPU2 conforme os acessos reais. Só promover cada etapa com execução,
   pixels/frames e evidência de input correspondentes. Regressões focadas e CI dos seis alvos.

Não alterar o launcher, READMEs ou contratos permanentes. Preservar ISO, firmware, NVM e saves.
Traces, extrações e traduções de conteúdo proprietário ficam apenas em `out/` ignorado.
