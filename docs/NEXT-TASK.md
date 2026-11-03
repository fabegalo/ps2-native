# Próximo incremento — completar o pipeline IRX/VU sem substituir o hardware

Direct ELF, SIF1 EE→IOP, VIF1 DIRECT→GIF→GS e overlays EE regenerados offline estão demonstrados
por programas próprios. O escopo integral M3.0 permanece aberto; ver [M3.0-STATUS](M3.0-STATUS.md).

1. **IRX:** implementar loader relocável R3000A e linking import/export com versões, usando o IOP
   existente. Testes próprios de módulos produtores/consumidores, HI16/LO16/R_MIPS_26, símbolos
   ausentes e recusas transacionais. O payload raw atual não comprova esse contrato. Auditar cada
   arquivo permissivo antes de adaptar Play!; não usar seu kernel HLE como substituto do original.
2. **VU0/VU1:** estabelecer um subconjunto micro executável com decode, hazards, E/end e delay slots
   documentados, compartilhando semântica entre interpretação e AOT. Implementar MPG/UNPACK/MSCAL
   em VIF somente junto com execução real. Estender o frame próprio para atravessar VU/GIF/GS;
   comparar registros, memória, eventos e digest, recusando instruções fora do subconjunto.
3. **SIF:** estender SIF1 para cadeia de origem/TTE/TIE, tags IRQ e quantidades parciais conforme
   evidência; depois SIF0 e entrega de interrupções EE. Validar latências/FIFO/arbitragem físicas
   antes de promover o throughput experimental atual a modelo de timing fiel.
4. **Serviços:** executar um homebrew próprio convencional só quando kernel/syscalls e dependências
   tiverem implementação demonstrada; não fornecer stubs de sucesso. Medir cobertura, trocas e tempo
   sob condições equivalentes e manter a CI de seis alvos.

O caminho ROM0/NVM original continua sendo requisito independente. Para avançar a SCPH-70004 além
do bloqueio existente ainda faltam IDs medidos do codificador de vídeo, fase vertical pós-reset e
INTC do EE; diagnóstico com valores assumidos não prova o Browser. Preservar essa investigação e
os dados compatíveis por console conforme [M2.9](M2.9-STATUS.md), sem usar Direct ELF como resultado
de compatibilidade de firmware.

Não editar READMEs ou contratos permanentes por incremento. Manter launcher/GUI separado,
programas próprios reproduzíveis, nenhum binário proprietário ou codegen de firmware no Git.
