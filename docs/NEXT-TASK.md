# Próxima execução — Super Bust-A-Move (SLUS-20115)

O checkpoint experimental já produziu 16 pacotes GIF PATH3 e 1.048.576 escritas
de pixels por IMAGE: limpeza real da VRAM comandada pelo jogo. Ainda nenhum
frame exibido, menu ou gameplay. Não apresentar a extração preta de VRAM como frame.

Última execução: ensaio 47, EE `0x100948`, 339.415.349 instruções; IOP ativo
`0xAE94`, 46.479.003. SUB.S passou; MUL.S com fs=`C31B0000`, ft=`453B8000`
para na fronteira de truncamento Booth. O produto inteiro `0x718680000000`
não permite escolher entre as mantissas `E30CFF`/`E30D00` pelo limite atual.
Caracterizar os carries descartados com referência verificável e implementação
BSD independente. Não trocar por float host, não relaxar o teste de intervalo,
não criar exceção por operandos/título/PC. Consultar PS2Recomp antes de implementar.
PS2Float não declara licença; não copiar, adaptar ou incorporar seu código.

Após a correção, repetir o jogo, registrar novo PC/contagens e continuar pelos
próximos bloqueios. O harness local está em `out/game-first/gs-iop-experiment-probe.cpp`,
build isolado `out/gs-iop-build`. Seu limite inicial de parada após novos pacotes
precisa ser ampliado se chegar ao orçamento de 340 milhões sem recusa, pois a
primeira limpeza já foi atingida. Os logs e artefatos comerciais continuam ignorados.

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
