# Próxima execução — Super Bust-A-Move (SLUS-20115)

ELF já executado; zero frames/menu/gameplay. Diagnóstico após BIOS parcial chega a
4.486.087 instruções (4.483.572 AOT), PC `0x80005F00`, leitura SMFLG.
O IOP parou em `0x40B0`: VBLANK depende do timing GS ainda não inicializado.
`SetGsCrt` Sony foi executado e também chegou ao encoder desconhecido; não inventar
IDs nem reiniciar o IOP para apagar seu histórico. Não confundir diagnóstico com ExecPS2.

SIF0/IRQ IP3 e equivalência AOT/interpretador têm regressões próprias. Investigar a
inicialização consistente do GS/IOP antes de avançar os seis módulos IOP do próprio disco.
No Simpsons, MUL.S `0x002DEAF4` passou; caso ambíguo seguinte em `0x001681B0`.
Estudar PS2Recomp antes de cada correção; sem copiar GPL, HLE substituto ou hacks por jogo.
Hashes/logs/evidência: [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md). Preservar launcher e dados locais.
