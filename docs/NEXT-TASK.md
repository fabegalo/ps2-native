# Próxima execução — Super Bust-A-Move (SLUS-20115)

O checkpoint experimental já produziu 35 pacotes GIF e 1.075.584 escritas
de pixels por IMAGE: limpeza real da VRAM comandada pelo jogo. Ainda nenhum
frame exibido, menu ou gameplay. Não apresentar a extração preta de VRAM como frame.

Última execução concluída: ensaio 94, EE `0x1CECA0`, 392.490.598 instruções,
Transferência PSMT4 64x96 ainda não suportada nessa execução. IOP `0x4CB18` / 53.113.412,
sem parada registrada. DMA VIF passou. Profundidade, scanout INT+FFMD e propagação de incerteza passaram. PSMT8 passou com os 26.624 índices reais. DMA GIF source-chain passou. PSMT4 implementado, seis testes focados aprovados; ensaio 95 em andamento.
D-cache funcional explícito desde reset passou por DXLTG: conteúdo separado
da RAM, LRF e writeback reais; sem latência adicional, I-cache não modelado.
Hipótese separada permite operações hit usando alias uncached da RAM.
Pads digitais 0/1 com botões soltos; comandos desconhecidos não recebem ACK.
Prazo serial 8192 ciclos, DMA SIO2 4 ciclos/palavra, RECV2.4/.5=0, FIELD=1
e fase GS-vsync 7/6 half-lines NTSC/PAL permanecem hipóteses.
Permanecem explícitas as hipóteses SPU2 (barramento 16 ciclos/palavra,
PIO 8 ciclos/halfword, ESA/EEA iniciais, área invertida inativa e readback
reservado), além de encoder/SYNCV e latches não atribuídos. Os demais bits RECV2 e RECV3 continuam opacos; nenhum valor de conexão é fabricado.
PMODE=66 e circuito 2 programados pelo jogo; scanout 640x224 válido, ainda inteiramente preto (limpeza de VRAM). Não é menu/gameplay.
Continuar reexecutando, registrando PCs e resolvendo os bloqueios até conteúdo
real exibido. Não trocar por float host nem criar exceções por título/PC.
Consultar PS2Recomp antes de implementar subsistemas; PS2Float não declara
licença e não pode ter código copiado, adaptado ou incorporado.

Após a correção, repetir o jogo, registrar novo PC/contagens e continuar pelos
próximos bloqueios. O harness local está em `out/game-first/gs-iop-experiment-probe.cpp`,
build isolado `out/gs-iop-build`. O limite de parada após novos pacotes foi removido;
executa até recusa ou orçamento de 1 bilhão. Diagnóstico de scanout/registradores
GS está ativo. Os logs e artefatos comerciais continuam ignorados.

O modo original foi preservado em todas as verificações: ROM0/NVM param no encoder
não medido, `0x80007D20`, 17.690.079 instruções. ELF injetado nesse estado para em
`0x80005F00`, 4.486.087 instruções, IOP `0x40B0`; timing GS desconhecido e zero pixels.
Hash canônico da RAM `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

A continuação experimental usa injeção de ELF e hipóteses explícitas de encoder,
SYNCV, F510, SBUS, DMACINTEN, IRQ inicial SIO2/SPU2 e timing CAV nominal. Não é
boot de disco concluído. ROM1 permanece vinculada explicitamente a hashes/perfil;
ROM2/EROM não fornecidas. Não transformar hipóteses em defaults, não instalar
módulos host, não fabricar respostas RPC e preservar os dados do usuário.

Detalhes, fontes, testes, CI e sequência completa em
[FIRST-GAME-STATUS](FIRST-GAME-STATUS.md). A CI de `dd6ed41`, que atualiza
uma fixture antiga após ADD.S, passou nos seis alvos do núcleo. O launcher
do commit FPU `1ddd0d1` passou nas seis arquiteturas.
