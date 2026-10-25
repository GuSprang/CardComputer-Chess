# v1.0.0 — Primeiro lançamento

Jogo de xadrez completo pro M5Stack Cardputer (original), com motor próprio.

## Funcionalidades
- 5 níveis de bot, do Iniciante ao Mestre, com livro de aberturas e busca alfa-beta.
- Modo 2 jogadores no mesmo aparelho.
- Entrada de lances por notação algébrica ou coordenadas.
- Roque, en passant, promoção, xeque e xeque-mate.
- Desfazer lance, girar tabuleiro, dica do bot.

## Instalação
1. Baixe o arquivo `cardputer_xadrez.bin` abaixo.
2. Entre em modo de gravação no Cardputer (segure **G0**, aperte e solte **Reset**, solte o G0).
3. Grave o `.bin` no endereço `0x0`, usando o [flasher do Bruce](https://bruce.computer/flasher) ou `esptool.py`/`esptool-js`.

## Requisitos
M5Stack Cardputer original (StampS3, sem "A" no nome do módulo).
