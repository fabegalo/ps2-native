# Próxima execução — Super Bust-A-Move (SLUS-20115)

Continuar o diagnóstico após os primeiros pacotes gráficos reais: ensaio 42
completou 16 pacotes GIF e 1.048.576 escritas de pixels, limpeza da VRAM feita
pelo jogo. Ainda nenhum frame exibido/menu/gameplay. Consultar PS2Recomp antes
de implementar cada subsistema; código próprio BSD, sem HLE ou respostas RPC falsas.

O modo original foi preservado: ROM0/NVM param no encoder não medido (`0x80007D20`);
o ELF injetado nesse estado para em `0x80005F00`, IOP `0x40B0`, timing GS desconhecido.
Obter evidência física do encoder/PCRTC continua necessário para remover as hipóteses.

A continuação experimental é separada e imprime suas hipóteses: encoder 7E/7F,
SYNCV de reset, omissão F510, configuração SBUS e DMACINTEN IOP=1. Com elas,
o firmware chega a `0x82000` após 32.416.676 instruções. Não é boot de disco concluído.
O jogo executa handlers Sony e transporte SIF real nos dois sentidos, sem fabricar
serviços. ReadCd/DMA3 leu IOPRP202 e módulos até o fim de RSPU2DRV.IRX (LBA 15824).
Com hipótese explícita de SPU2 inativo no reset, o ensaio 42 alcançou 5.264 bytes
SIF1 / 2.592 SIF0; IOP ativo `0xAE98`, 46.323.687 instruções. EE `0x1CDF74`,
338.172.827 instruções: LD do GS CSR para ler REV. A observação parcial agora
usa revisão/ID medidos no perfil e preserva FIELD/NFIELD desconhecidos; reexecutar.
Contexto COP0, IRQ DMA pendente, ROM1, CDVD, NEXT e versão MECHACON foram superados.
Continuar os próximos comandos gráficos e de dispositivos pelo código convidado.

Salvar/restaurar BPC/PCCR desabilitados mantém seus campos indefinidos. A ROM1
exige vínculo explícito de hashes/perfil/metadados e não há ROM2/EROM fornecidas.
Continuar com a imagem local CD Mode2/2352 explicitamente montada, com hipótese
CAV nominal registrada, e executar novamente após cada correção. Não abreviar os
loops de retry do jogo nem transformar as opções de
diagnóstico em defaults. Somente declarar frame a partir dos comandos gráficos reais.

Evidência, hashes, sequência de bloqueios e limites em
[FIRST-GAME-STATUS](FIRST-GAME-STATUS.md). Logs/harness locais ignorados em
`out/game-first/gs-iop-experiment-*`; build isolado `out/gs-iop-build`.
Preservar firmware, dados locais e launcher. Os módulos IOP do disco ainda precisam
ser executados pelo sistema convidado; não instalar substitutos host.
