# Próxima execução — Super Bust-A-Move (SLUS-20115)

Continuar o diagnóstico do jogo até o primeiro pacote gráfico real. Ainda há zero
pacotes GIF/pixels do jogo, nenhum frame/menu/gameplay. Consultar PS2Recomp antes
de implementar cada subsistema; código próprio BSD, sem HLE ou respostas RPC falsas.

O modo original foi preservado: ROM0/NVM param no encoder não medido (`0x80007D20`);
o ELF injetado nesse estado para em `0x80005F00`, IOP `0x40B0`, timing GS desconhecido.
Obter evidência física do encoder/PCRTC continua necessário para remover as hipóteses.

A continuação experimental é separada e imprime suas hipóteses: encoder 7E/7F,
SYNCV de reset, omissão F510, configuração SBUS e DMACINTEN IOP=1. Com elas,
o firmware chega a `0x82000` após 32.416.676 instruções. Não é boot de disco concluído.
O jogo executa handlers Sony e transporte SIF real nos dois sentidos, sem fabricar
serviços: 1.664 bytes SIF1 / 1.536 SIF0. O ensaio 33 executou 154.883.782
instruções e parou no status CDVD `0x1F40200B`, IOP `0x1AEE8`, EE `0x80005F00`.
Contexto COP0, IRQ pendente ao reativar DICR, ROM1 e IRQ SIO2 ocioso foram superados.

Salvar/restaurar BPC/PCCR desabilitados mantém seus campos indefinidos. A ROM1
exige vínculo explícito de hashes/perfil/metadados e não há ROM2/EROM fornecidas.
Continuar com a imagem local CD Mode2/2352 explicitamente montada, seguir o
próximo comando CDVD e executar novamente após cada correção. Não abreviar os
loops de retry do jogo nem transformar as opções de
diagnóstico em defaults. Somente declarar frame a partir dos comandos gráficos reais.

Evidência, hashes, sequência de bloqueios e limites em
[FIRST-GAME-STATUS](FIRST-GAME-STATUS.md). Logs/harness locais ignorados em
`out/game-first/gs-iop-experiment-*`; build isolado `out/gs-iop-build`.
Preservar firmware, dados locais e launcher. Os módulos IOP do disco ainda precisam
ser executados pelo sistema convidado; não instalar substitutos host.
