# Próxima execução — Super Bust-A-Move (SLUS-20115)

O checkpoint experimental já produziu 16 pacotes GIF PATH3 e 1.048.576 escritas
de pixels por IMAGE: limpeza real da VRAM comandada pelo jogo. Ainda nenhum
frame exibido, menu ou gameplay. Não apresentar a extração preta de VRAM como frame.

Última execução concluída: ensaio 75, EE em espera `0x81FD4`, 341.472.144 instruções;
IOP parou em `0x666C0`, 46.736.104, configuração do primeiro deslocamento APF. PIO transferiu 16 palavras reais.
Ensaio 76 repete após configurar taps/coeficientes; conserva hipótese opt-in de bits não usados ESA-high/EEA lidos como zero; preparado com hipótese opt-in de ESA interno EFFF8/FFFF8, sem readback, além de EEA E/F (PIO = 8 ciclos IOP/halfword, hipótese), com hipóteses opt-in de 7CA e janela não atribuída
0x346..0x3FF, 0x746..0x75F, 0x7B0..0x7BF/espelhos. Modo original mantém recusas; leituras não são fabricadas.
PMODE ainda não foi escrito; sem scanout.
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
