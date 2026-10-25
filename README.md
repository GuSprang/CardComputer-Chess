# Cardputer Xadrez

**[🇧🇷 Português](README.md) | [🇺🇸 English](README.en.md)**

Um jogo de xadrez completo para o **M5Stack Cardputer (original, não ADV)**, com motor próprio, livro de aberturas e 5 níveis de dificuldade.

![plataforma](https://img.shields.io/badge/platform-M5Stack%20Cardputer-informational)
![licença](https://img.shields.io/badge/license-MIT-green)

## Funcionalidades

- **Bilíngue**: português e inglês, selecionável no menu inicial (opção "Idioma").
- **Dois modos**: jogue contra o bot ou com outra pessoa no mesmo aparelho (hotseat).
- **5 níveis de bot** (Iniciante a Mestre): livro de aberturas, busca alfa-beta com tabela de transposição, avaliação com estrutura de peões, segurança do rei e finais, e "deslizes" humanos nos níveis mais baixos (o bot não joga sempre perfeito).
- **Entrada por texto**: digite o lance em notação algébrica (`e4`, `Nf3`, `exd5`, `O-O`, `e8=Q`) ou em coordenadas (`e2e4`, `e7e8q`).
- **Roque, en passant e promoção** completos, incluindo detecção de xeque e xeque-mate.
- **Comandos rápidos**: desfazer lance, girar tabuleiro, pedir dica ao bot, ligar/desligar som.

## Requisitos

- **Hardware**: Cardputer (não compatível oficialmente testado no ADV, mas deve funcionar).

### Para a compilação a partir do .ino
- **Arduino IDE** com suporte a placas M5Stack instalado.
- **Biblioteca `M5Cardputer`** (instala `M5Unified` e `M5GFX` junto).
- Placa selecionada: `M5Stack > M5Cardputer`.

## Instalação

### Opção 1 — Firmware pronto
Baixe o `.bin` da aba **[Releases](../../releases)** deste repositório e grave no seu Cardputer:

- Pelo [flasher web do Bruce](https://bruce.computer/flasher) ou qualquer flasher baseado em `esptool-js`/`esptool.py`.
- Entre em modo de gravação: segure o botão **G0**, aperte e solte o **Reset**, solte o G0.
- Grave o `.bin` no endereço `0x0`.

### Opção 2 — Compilar você mesmo
1. Abra `cardputer_xadrez.ino` na Arduino IDE.
2. Selecione a placa `M5Stack > M5Cardputer`.
3. Compile e envie (`Sketch > Upload`).

## Como jogar

No menu inicial, use:

| Tecla | Ação |
|---|---|
| `;` | cima |
| `.` | baixo |
| `,` | esquerda |
| `/` | direita |
| `Enter` | confirma |

Durante a partida, digite o lance e aperte **Enter**. Comandos especiais (digite e dê Enter):

| Comando | Atalho | Ação |
|---|---|---|
| `menu` | `m` | volta ao menu |
| `undo` | `u` | desfaz lance |
| `flip` | `f` | gira o tabuleiro |
| `hint` | `h` | sugestão do bot |
| `snd` | — | liga/desliga som |

## Níveis do bot

| Nível | Profundidade | Livro de aberturas | Observação |
|---|---|---|---|
| 1 — Iniciante | 1 ply | — | lances bem aleatórios |
| 2 — Fácil | 2 plies | 6 plies | erra bastante |
| 3 — Médio | 3 plies | 12 plies | joga razoável |
| 4 — Difícil | 5 plies | 20 plies | poucos erros |
| 5 — Mestre | 8 plies | 24 plies | joga forte |

## Licença

Distribuído sob a licença MIT — veja [LICENSE](LICENSE).

## Contribuindo

Sugestões, issues e pull requests são bem-vindos.
