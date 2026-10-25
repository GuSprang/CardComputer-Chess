/*
  XADREZ para M5Stack Cardputer (original, nao ADV)

  - Digite o lance em notacao algebrica (e4, Nf3, exd5, O-O, e8=Q) ou
    coordenadas (e2e4, e7e8q) e aperte ENTER.
  - Modos: vs Bot (5 niveis) ou 2 jogadores no mesmo aparelho.
  - Bot: livro de aberturas, busca alfa-beta com tabela de transposicao,
    avaliacao com estrutura de peoes, seguranca do rei e finais, e erros
    "humanos" nos niveis baixos (nao joga sempre igual).

  Bibliotecas: M5Cardputer (instala M5Unified e M5GFX junto)
  Placa (Arduino IDE): M5Stack > M5Cardputer

  Comandos digitados no campo de lance:
    menu (m)  volta ao menu      undo (u)  desfaz lance
    flip (f)  gira o tabuleiro   hint (h)  sugestao do bot
    snd       liga/desliga som

  Menu: ; = cima   . = baixo   , = esquerda   / = direita   ENTER = confirma
*/

#include <M5Cardputer.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#define LCD M5Cardputer.Display

// ============================================================
//  Tipos e constantes
// ============================================================
#define F_CAP    1
#define F_EP     2
#define F_CASTLE 4
#define F_DBL    8

struct Move { uint8_t from, to, promo, flags; };

// Nivel do bot (fica aqui no topo por causa dos prototipos automaticos da Arduino IDE)
struct Lvl { uint8_t depth; uint16_t ms; int16_t noise; uint8_t rnd; uint8_t err; int16_t errMargin; uint8_t book; };

struct Pos {
  int8_t   b[64];      // indice = rank*8 + file (a1 = 0). + brancas, - pretas
  uint8_t  side;       // 0 brancas, 1 pretas
  uint8_t  castle;     // 1 WK, 2 WQ, 4 BK, 8 BQ
  int8_t   ep;         // casa de en-passant ou -1
  uint8_t  half;       // relogio dos 50 lances
  uint8_t  ksq[2];     // posicao dos reis
  uint16_t full;       // numero do lance
  uint64_t key;        // hash Zobrist
};

// ---------- Hash Zobrist ----------
static uint64_t ZP[13][64], ZC[16], ZE[8], ZS;
static uint64_t rngState = 0x9E3779B97F4A7C15ULL;
static uint64_t rng64() {
  rngState ^= rngState << 13; rngState ^= rngState >> 7; rngState ^= rngState << 17;
  return rngState;
}
void initZobrist() {
  for (int i = 0; i < 13; i++) for (int j = 0; j < 64; j++) ZP[i][j] = rng64();
  for (int i = 0; i < 16; i++) ZC[i] = rng64();
  for (int i = 0; i < 8; i++) ZE[i] = rng64();
  ZS = rng64();
}
uint64_t computeKey(const Pos &p) {
  uint64_t k = 0;
  for (int i = 0; i < 64; i++) if (p.b[i]) k ^= ZP[p.b[i] + 6][i];
  k ^= ZC[p.castle];
  if (p.ep >= 0) k ^= ZE[p.ep & 7];
  if (p.side) k ^= ZS;
  return k;
}

// ---------- Historico da partida ----------
#define HMAX 400
Pos     H[HMAX];
char    SAN[HMAX][10];
uint8_t LF[HMAX], LT[HMAX];
int     hp = 0;
// pecas: 1 P, 2 N, 3 B, 4 R, 5 Q, 6 K

static const char PCH[] = " PNBRQK";
static const int8_t DIRS[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
static const int8_t KNM[8][2]  = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
static const int VAL[7] = {0, 100, 320, 330, 500, 900, 20000};
static const int8_t PAWN_RANK[8] = {0, 0, 4, 10, 20, 35, 60, 0};

#define INF   30000
#define MATE  20000
#define MAXPLY 32

// ============================================================
//  Geracao de lances
// ============================================================
void initPos(Pos &p) {
  const int8_t back[8] = {4, 2, 3, 5, 6, 3, 2, 4};
  memset(&p, 0, sizeof(p));
  for (int f = 0; f < 8; f++) {
    p.b[f] = back[f];
    p.b[8 + f] = 1;
    p.b[48 + f] = -1;
    p.b[56 + f] = -back[f];
  }
  p.side = 0; p.castle = 15; p.ep = -1; p.half = 0; p.full = 1;
  p.ksq[0] = 4; p.ksq[1] = 60;
  p.key = computeKey(p);
}

bool attacked(const Pos &p, int sq, int by) {
  int f = sq & 7, r = sq >> 3;
  int sg = by ? -1 : 1;
  int pr = r - (by ? -1 : 1);
  if (pr >= 0 && pr < 8) {
    if (f > 0 && p.b[pr * 8 + f - 1] == sg) return true;
    if (f < 7 && p.b[pr * 8 + f + 1] == sg) return true;
  }
  for (int k = 0; k < 8; k++) {
    int nf = f + KNM[k][0], nr = r + KNM[k][1];
    if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
    if (p.b[nr * 8 + nf] == sg * 2) return true;
  }
  for (int k = 0; k < 8; k++) {
    int nf = f + DIRS[k][0], nr = r + DIRS[k][1];
    if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
    if (p.b[nr * 8 + nf] == sg * 6) return true;
  }
  for (int k = 0; k < 8; k++) {
    int nf = f + DIRS[k][0], nr = r + DIRS[k][1];
    while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
      int8_t pc = p.b[nr * 8 + nf];
      if (pc) {
        if (pc == sg * 5 || pc == sg * (k < 4 ? 4 : 3)) return true;
        break;
      }
      nf += DIRS[k][0]; nr += DIRS[k][1];
    }
  }
  return false;
}

inline bool inCheck(const Pos &p, int side) { return attacked(p, p.ksq[side], side ^ 1); }

int genMoves(const Pos &p, Move *mv, bool caps) {
  int n = 0;
  const int s = p.side;
  auto add = [&](int fr, int to, int pr, int fl) {
    if (n < 255) { mv[n].from = fr; mv[n].to = to; mv[n].promo = pr; mv[n].flags = fl; n++; }
  };
  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (!pc) continue;
    if ((pc > 0) != (s == 0)) continue;
    int a = abs(pc), f = sq & 7, r = sq >> 3;

    if (a == 1) {
      int dir = s ? -1 : 1, nr = r + dir;
      if (nr < 0 || nr > 7) continue;
      bool pr = (nr == (s ? 0 : 7));
      int to = nr * 8 + f;
      if (!p.b[to]) {
        if (pr) {
          add(sq, to, 5, 0);
          if (!caps) { add(sq, to, 4, 0); add(sq, to, 3, 0); add(sq, to, 2, 0); }
        } else if (!caps) {
          add(sq, to, 0, 0);
          if (r == (s ? 6 : 1) && !p.b[to + dir * 8]) add(sq, to + dir * 8, 0, F_DBL);
        }
      }
      for (int df = -1; df <= 1; df += 2) {
        int nf = f + df;
        if (nf < 0 || nf > 7) continue;
        int t = nr * 8 + nf;
        int8_t tp = p.b[t];
        if (tp && ((tp > 0) == (s == 1))) {
          if (pr) {
            add(sq, t, 5, F_CAP);
            if (!caps) { add(sq, t, 4, F_CAP); add(sq, t, 3, F_CAP); add(sq, t, 2, F_CAP); }
          } else add(sq, t, 0, F_CAP);
        } else if (!tp && t == p.ep) {
          add(sq, t, 0, F_EP | F_CAP);
        }
      }
    } else if (a == 2 || a == 6) {
      const int8_t (*offs)[2] = (a == 2) ? KNM : DIRS;
      for (int k = 0; k < 8; k++) {
        int nf = f + offs[k][0], nr = r + offs[k][1];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
        int t = nr * 8 + nf;
        int8_t tp = p.b[t];
        if (!tp) { if (!caps) add(sq, t, 0, 0); }
        else if ((tp > 0) == (s == 1)) add(sq, t, 0, F_CAP);
      }
      if (a == 6 && !caps) {
        int base = s ? 56 : 0;
        if (sq == base + 4) {
          uint8_t kb = s ? 4 : 1, qb = s ? 8 : 2;
          int8_t rook = s ? -4 : 4;
          if ((p.castle & kb) && !p.b[base + 5] && !p.b[base + 6] && p.b[base + 7] == rook &&
              !attacked(p, base + 4, s ^ 1) && !attacked(p, base + 5, s ^ 1))
            add(sq, base + 6, 0, F_CASTLE);
          if ((p.castle & qb) && !p.b[base + 1] && !p.b[base + 2] && !p.b[base + 3] && p.b[base] == rook &&
              !attacked(p, base + 4, s ^ 1) && !attacked(p, base + 3, s ^ 1))
            add(sq, base + 2, 0, F_CASTLE);
        }
      }
    } else {
      int k0 = (a == 3) ? 4 : 0, k1 = (a == 4) ? 4 : 8;
      for (int k = k0; k < k1; k++) {
        int nf = f + DIRS[k][0], nr = r + DIRS[k][1];
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          int t = nr * 8 + nf;
          int8_t tp = p.b[t];
          if (!tp) { if (!caps) add(sq, t, 0, 0); }
          else {
            if ((tp > 0) == (s == 1)) add(sq, t, 0, F_CAP);
            break;
          }
          nf += DIRS[k][0]; nr += DIRS[k][1];
        }
      }
    }
  }
  return n;
}

void doMove(Pos &p, const Move &m) {
  int8_t pc = p.b[m.from];
  int s = p.side, a = abs(pc);
  int8_t tp = p.b[m.to];
  uint64_t k = p.key;
  p.half = (a == 1 || tp || (m.flags & F_EP)) ? 0 : p.half + 1;
  k ^= ZP[pc + 6][m.from];
  if (tp) k ^= ZP[tp + 6][m.to];
  p.b[m.to] = pc;
  p.b[m.from] = 0;
  if (m.flags & F_EP) {
    int cs = m.to + (s == 0 ? -8 : 8);
    k ^= ZP[p.b[cs] + 6][cs];
    p.b[cs] = 0;
  }
  int8_t placed = pc;
  if (m.promo) { placed = s == 0 ? m.promo : -m.promo; p.b[m.to] = placed; }
  k ^= ZP[placed + 6][m.to];
  if (m.flags & F_CASTLE) {
    int base = m.to >= 56 ? 56 : 0;
    int rf, rt;
    if ((m.to & 7) == 6) { rf = base + 7; rt = base + 5; }
    else                 { rf = base;     rt = base + 3; }
    int8_t rk = p.b[rf];
    p.b[rt] = rk; p.b[rf] = 0;
    k ^= ZP[rk + 6][rf] ^ ZP[rk + 6][rt];
  }
  if (a == 6) p.ksq[s] = m.to;
  if (p.ep >= 0) k ^= ZE[p.ep & 7];
  p.ep = (m.flags & F_DBL) ? (m.from + m.to) / 2 : -1;
  if (p.ep >= 0) k ^= ZE[p.ep & 7];
  uint8_t oc = p.castle;
  auto clr = [&](int sq) {
    switch (sq) {
      case 4:  p.castle &= ~3;  break;
      case 60: p.castle &= ~12; break;
      case 0:  p.castle &= ~2;  break;
      case 7:  p.castle &= ~1;  break;
      case 56: p.castle &= ~8;  break;
      case 63: p.castle &= ~4;  break;
    }
  };
  clr(m.from); clr(m.to);
  k ^= ZC[oc] ^ ZC[p.castle];
  if (s == 1) p.full++;
  p.side ^= 1;
  k ^= ZS;
  p.key = k;
}

int genLegal(const Pos &p, Move *out) {
  int n = genMoves(p, out, false), k = 0;
  for (int i = 0; i < n; i++) {
    Pos c = p;
    doMove(c, out[i]);
    if (!attacked(c, c.ksq[p.side], c.side)) out[k++] = out[i];
  }
  return k;
}

// ============================================================
//  Avaliacao e busca
// ============================================================
static const int8_t PASSED[8] = {0, 0, 10, 20, 35, 60, 100, 0};

int evaluate(const Pos &p) {
  auto cenOf = [](int q) {
    int f = q & 7, r = q >> 3;
    int df = f < 7 - f ? f : 7 - f;
    int dr = r < 7 - r ? r : 7 - r;
    return (df + dr) / 2;
  };
  int sc = 0, npm = 0, matD = 0;
  int bishops[2] = {0, 0}, undev[2] = {0, 0};
  int kpos[2] = {4, 60};
  uint8_t fc[2][8];
  int8_t hi[2][8], lo[2][8];
  for (int s2 = 0; s2 < 2; s2++)
    for (int f2 = 0; f2 < 8; f2++) { fc[s2][f2] = 0; hi[s2][f2] = -1; lo[s2][f2] = 8; }
  uint8_t pl[2][10], rl[2][4];
  int np[2] = {0, 0}, nr[2] = {0, 0};

  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (!pc) continue;
    int a = abs(pc), sd = pc > 0 ? 0 : 1;
    int f = sq & 7, r = sq >> 3;
    int rr = sd ? 7 - r : r;
    int cen = cenOf(sq);
    int v = VAL[a];
    if (a != 6) matD += sd ? -VAL[a] : VAL[a];
    switch (a) {
      case 1:
        v += PAWN_RANK[rr];
        if ((f == 3 || f == 4) && rr < 5) v += 6;
        fc[sd][f]++;
        if (r > hi[sd][f]) hi[sd][f] = r;
        if (r < lo[sd][f]) lo[sd][f] = r;
        if (np[sd] < 10) pl[sd][np[sd]++] = sq;
        break;
      case 2: v += cen * 10; npm += 320; if (rr == 0) undev[sd]++; break;
      case 3: v += cen * 5; bishops[sd]++; npm += 330; if (rr == 0) undev[sd]++; break;
      case 4: npm += 500; if (rr == 6) v += 20; if (nr[sd] < 4) rl[sd][nr[sd]++] = sq; break;
      case 5: npm += 900; break;
      case 6: kpos[sd] = sq; v = 0; break;
    }
    sc += sd ? -v : v;
  }

  // K+B ou K+N contra K: empate
  if (np[0] + np[1] == 0 && npm <= 330) return 0;
  bool eg = npm < 2800;

  for (int sd = 0; sd < 2; sd++) {
    // estrutura de peoes
    for (int i = 0; i < np[sd]; i++) {
      int sq = pl[sd][i], f = sq & 7, r = sq >> 3;
      int rr = sd ? 7 - r : r, v = 0;
      if (fc[sd][f] > 1) v -= 8;                                   // dobrado
      bool iso = !(f > 0 && fc[sd][f - 1]) && !(f < 7 && fc[sd][f + 1]);
      if (iso) v -= 12;                                            // isolado
      bool passed = true;
      for (int ff = f - 1; ff <= f + 1; ff++) {
        if (ff < 0 || ff > 7) continue;
        if (sd == 0) { if (hi[1][ff] > r) passed = false; }
        else         { if (lo[0][ff] < r) passed = false; }
      }
      if (passed) v += PASSED[rr] + (eg ? PASSED[rr] / 2 : 0);     // passado
      sc += sd ? -v : v;
    }
    // torres em colunas abertas / semiabertas
    for (int i = 0; i < nr[sd]; i++) {
      int f = rl[sd][i] & 7, v = 0;
      if (!fc[sd][f]) v = fc[1 - sd][f] ? 10 : 20;
      sc += sd ? -v : v;
    }
  }

  // desenvolvimento das pecas leves
  if (!eg) sc += (undev[1] - undev[0]) * 8;

  // reis
  for (int sd = 0; sd < 2; sd++) {
    int sq = kpos[sd], f = sq & 7, r = sq >> 3;
    int rr = sd ? 7 - r : r;
    int v;
    if (eg) v = cenOf(sq) * 12;
    else {
      v = (rr == 0 && (f <= 2 || f >= 6)) ? 30 : 0;
      v -= rr * 25;
      if (rr <= 1) {                                               // escudo de peoes
        int dirr = sd ? -1 : 1;
        int8_t sg = sd ? -1 : 1;
        for (int ff = f - 1; ff <= f + 1; ff++) {
          if (ff < 0 || ff > 7) continue;
          int r1 = r + dirr, r2 = r + 2 * dirr;
          if (r1 >= 0 && r1 < 8 && p.b[r1 * 8 + ff] == sg) v += 10;
          else if (r2 >= 0 && r2 < 8 && p.b[r2 * 8 + ff] == sg) v += 5;
          else v -= 8;
        }
      }
    }
    sc += sd ? -v : v;
  }

  // final: empurrar o rei adversario para a borda e aproximar o proprio rei
  if (eg && (matD >= 250 || matD <= -250)) {
    int L = matD > 0 ? 0 : 1, E = 1 - L;
    int ke = kpos[E], kl = kpos[L];
    int ed = 3 - cenOf(ke);
    int dist = abs((ke & 7) - (kl & 7)) + abs((ke >> 3) - (kl >> 3));
    int mop = ed * 14 + (14 - dist) * 4;
    sc += (L == 0) ? mop : -mop;
  }

  if (bishops[0] >= 2) sc += 30;
  if (bishops[1] >= 2) sc -= 30;
  sc += (p.side == 0) ? 8 : -8;
  return p.side == 0 ? sc : -sc;
}

// ---------- Tabela de transposicao ----------
struct TTE { uint32_t chk; int16_t score; int8_t depth; uint8_t flag; uint8_t from, to, promo, pad; };
static TTE *TT = nullptr;
static uint32_t TTMASK = 0;

void ttInit() {
  uint32_t n = 8192;
  while (n >= 1024) {
    TT = (TTE *)malloc(n * sizeof(TTE));
    if (TT) { TTMASK = n - 1; memset(TT, 0, n * sizeof(TTE)); return; }
    n >>= 1;
  }
  TT = nullptr;
}
void ttClear() { if (TT) memset(TT, 0, (TTMASK + 1) * sizeof(TTE)); }

// ---------- Busca ----------
static Move    movePool[MAXPLY][256];
static int16_t scorePool[MAXPLY][256];
static uint16_t killers[MAXPLY][2];
static uint32_t nodes, deadline;
static bool stopFlag;
static int rootLimit;

int16_t orderScore(const Pos &p, const Move &m) {
  int v = 0;
  int8_t vic = p.b[m.to];
  if (vic) v = VAL[abs(vic)] * 10 - VAL[abs(p.b[m.from])] / 10;
  else if (m.flags & F_EP) v = 1000 - 10;
  if (m.promo) v += VAL[m.promo];
  return (int16_t)v;
}

static inline void pickBest(Move *mv, int16_t *sc, int i, int n) {
  int bi = i;
  for (int j = i + 1; j < n; j++) if (sc[j] > sc[bi]) bi = j;
  if (bi != i) { Move t = mv[i]; mv[i] = mv[bi]; mv[bi] = t; int16_t ts = sc[i]; sc[i] = sc[bi]; sc[bi] = ts; }
}

static inline void tick() {
  if ((++nodes & 511) == 0) {
    if (millis() > deadline) stopFlag = true;
    if ((nodes & 8191) == 0) yield();
  }
}

int qsearch(const Pos &p, int alpha, int beta, int ply) {
  tick();
  if (stopFlag) return 0;
  int stand = evaluate(p);
  if (ply >= MAXPLY - 1) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  Move *mv = movePool[ply];
  int16_t *sc = scorePool[ply];
  int n = genMoves(p, mv, true);
  for (int i = 0; i < n; i++) sc[i] = orderScore(p, mv[i]);
  for (int i = 0; i < n; i++) {
    pickBest(mv, sc, i, n);
    Pos c = p;
    doMove(c, mv[i]);
    if (attacked(c, c.ksq[p.side], c.side)) continue;
    int v = -qsearch(c, -beta, -alpha, ply + 1);
    if (stopFlag) return 0;
    if (v >= beta) return v;
    if (v > alpha) alpha = v;
  }
  return alpha;
}

int negamax(const Pos &p, int depth, int alpha, int beta, int ply) {
  tick();
  if (stopFlag) return 0;
  if (p.half >= 100) return 0;
  if (ply >= MAXPLY - 2) return evaluate(p);
  bool chk = inCheck(p, p.side);
  if (chk && ply < rootLimit + 2) depth++;
  if (depth <= 0) return qsearch(p, alpha, beta, ply);

  int alphaOrig = alpha;
  bool hasTT = false;
  uint8_t tfrom = 0, tto = 0, tpromo = 0;
  TTE *e = nullptr;
  if (TT) {
    e = &TT[(uint32_t)p.key & TTMASK];
    if (e->chk == (uint32_t)(p.key >> 32)) {
      hasTT = true; tfrom = e->from; tto = e->to; tpromo = e->promo;
      if (e->depth >= depth) {
        int s2 = e->score;
        if (s2 > MATE - 100) s2 -= ply; else if (s2 < -MATE + 100) s2 += ply;
        if (e->flag == 0) return s2;
        if (e->flag == 1 && s2 >= beta) return s2;
        if (e->flag == 2 && s2 <= alpha) return s2;
      }
    }
  }

  Move *mv = movePool[ply];
  int16_t *sc = scorePool[ply];
  int n = genMoves(p, mv, false);
  for (int i = 0; i < n; i++) {
    int v = orderScore(p, mv[i]);
    bool quiet = !p.b[mv[i].to] && !(mv[i].flags & F_EP);
    if (quiet) {
      uint16_t kk = mv[i].from * 64 + mv[i].to;
      if (kk == killers[ply][0] || kk == killers[ply][1]) v += 800;
    }
    if (hasTT && mv[i].from == tfrom && mv[i].to == tto && mv[i].promo == tpromo) v = 32000;
    sc[i] = (int16_t)v;
  }

  int best = -INF, legalCount = 0;
  Move bm = {0, 0, 0, 0};
  for (int i = 0; i < n; i++) {
    pickBest(mv, sc, i, n);
    Pos c = p;
    doMove(c, mv[i]);
    if (attacked(c, c.ksq[p.side], c.side)) continue;
    legalCount++;
    int v = -negamax(c, depth - 1, -beta, -alpha, ply + 1);
    if (stopFlag) return 0;
    if (v > best) { best = v; bm = mv[i]; }
    if (v > alpha) alpha = v;
    if (alpha >= beta) {
      if (!p.b[mv[i].to] && !(mv[i].flags & F_EP)) {
        uint16_t kk = mv[i].from * 64 + mv[i].to;
        if (killers[ply][0] != kk) { killers[ply][1] = killers[ply][0]; killers[ply][0] = kk; }
      }
      break;
    }
  }
  if (!legalCount) return chk ? -MATE + ply : 0;

  if (TT && e) {
    int st = best;
    if (st > MATE - 100) st += ply; else if (st < -MATE + 100) st -= ply;
    e->chk = (uint32_t)(p.key >> 32);
    e->score = (int16_t)st;
    e->depth = (int8_t)depth;
    e->flag = (best <= alphaOrig) ? 2 : (best >= beta ? 1 : 0);
    e->from = bm.from; e->to = bm.to; e->promo = bm.promo;
  }
  return best;
}

// ---------- Niveis do bot ----------
// depth: profundidade maxima | ms: tempo maximo | noise: ruido na nota (cp)
// rnd: % de lances totalmente aleatorios | err: % de "deslizes" humanos
// errMargin: o deslize escolhe entre lances ate X cp piores que o melhor
// book: ate quantos lances (plies) usa o livro de aberturas
const Lvl LV[5] = {
  {1,   500, 300, 25,  0,   0,  0},   // 1 Iniciante
  {2,  1500,  80,  0, 30, 250,  6},   // 2 Facil
  {3,  3000,  35,  0, 20, 150, 12},   // 3 Medio
  {5,  6000,  15,  0,  8,  80, 20},   // 4 Dificil
  {8, 12000,   6,  0,  0,   0, 24}    // 5 Mestre
};
const char *LVN_PT[5] = {"Iniciante", "Facil", "Medio", "Dificil", "Mestre"};
const char *LVN_EN[5] = {"Beginner", "Easy", "Medium", "Hard", "Master"};
#define LVN(i) (lang == 0 ? LVN_PT[i] : LVN_EN[i])

// ---------- Livro de aberturas (coordenadas) ----------
static const char *const BOOK[] = {
  // 1.e4 e5
  "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8",
  "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6 e1g1 e8g8",
  "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8e7 e1g1 e8g8",
  "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6 e4e5 d8e7",
  "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3 f8e7 e1g1 b8c6",
  "e2e4 e7e5 g1f3 d7d6 d2d4 g8f6 b1c3 b8d7 f1c4 f8e7 e1g1 e8g8",
  "e2e4 e7e5 b1c3 b8c6 g2g3 g8f6 f1g2 f8c5 g1e2 d7d6 e1g1 e8g8",
  // Siciliana
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5 d4b3 c8e6",
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 e8g8 d1d2 b8c6",
  "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6",
  "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 f1d3 g8f6 e1g1 d8c7",
  "e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4 g1f3 b8c6 c3d4 d7d6",
  // Francesa
  "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7 g5e7 d8e7 f2f4 e8g8",
  "e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6",
  "e2e4 e7e6 d2d4 d7d5 b1d2 g8f6 e4e5 f6d7 f1d3 c7c5 c2c3 b8c6 g1e2 c5d4",
  // Caro-Kann
  "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3 b8d7 h4h5 g6h7 f1d3 h7d3 d1d3 e7e6",
  "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6 f1e2 c6c5 e1g1",
  // Outras respostas a 1.e4
  "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c7c6 f1c4 c8f5",
  "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f2f4 f8g7 g1f3 e8g8",
  "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3 c8g4 f1e2 e7e6 e1g1 f8e7",
  // 1.d4
  "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 b8d7",
  "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1 a7a6",
  "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6 f1c4 f8b4",
  "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 e2e3 e7e6 b1d2 b8d7 f1d3 d5c4 d3c4 b7b5",
  "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3 c7c5 e1g1 b8c6",
  "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6",
  "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7 g1f3 c7c5",
  "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7 e1g1 e8g8 b1c3 f6e4",
  "d2d4 d7d5 g1f3 g8f6 c1f4 e7e6 e2e3 c7c5 c2c3 b8c6 b1d2 f8d6 f4g3 e8g8",
  "d2d4 f7f5 g2g3 g8f6 f1g2 e7e6 g1f3 f8e7 e1g1 e8g8 c2c4 d7d6",
  // Inglesa e Reti
  "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5 f1g2 d5b6 e1g1 f8e7",
  "c2c4 c7c5 g1f3 g8f6 b1c3 e7e6 g2g3 b7b6 f1g2 c8b7 e1g1 f8e7",
  "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d4 e8g8 c2c4 d5c4",
  "g1f3 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 d2d4 e8g8"
};

// ---------- Estado de repeticao (historico da partida) ----------
static int repOf(const Pos &c) {
  int cnt = 0;
  for (int i = hp; i >= 0; i--) {
    if (H[i].key == c.key) cnt++;
    if (H[i].half == 0) break;
  }
  return cnt;
}

struct RootE { Move m; int16_t s; };
static RootE R[256];
static int16_t tmpS[256];
static Move rootMv[256];

static void sortRoot(int n) {
  for (int i = 1; i < n; i++) {
    RootE x = R[i]; int j = i - 1;
    while (j >= 0 && R[j].s < x.s) { R[j + 1] = R[j]; j--; }
    R[j + 1] = x;
  }
}

// procura um lance no livro; usa rootMv (n lances legais) para validar
static bool bookMove(int maxPly, int n, Move &out) {
  if (hp >= maxPly) return false;
  uint8_t cf[48], ct[48];
  int nc = 0;
  const int NB = sizeof(BOOK) / sizeof(BOOK[0]);
  for (int b = 0; b < NB; b++) {
    const char *s = BOOK[b];
    bool ok = true;
    for (int i = 1; i <= hp; i++) {
      if (!s[0] || !s[1] || !s[2] || !s[3]) { ok = false; break; }
      int f = (s[1] - '1') * 8 + (s[0] - 'a'), t = (s[3] - '1') * 8 + (s[2] - 'a');
      if (f != LF[i] || t != LT[i]) { ok = false; break; }
      s += 4;
      if (*s == ' ') s++;
    }
    if (!ok || !s[0] || !s[1] || !s[2] || !s[3]) continue;
    if (nc < 48) {
      cf[nc] = (s[1] - '1') * 8 + (s[0] - 'a');
      ct[nc] = (s[3] - '1') * 8 + (s[2] - 'a');
      nc++;
    }
  }
  if (!nc) return false;
  int pick = random(nc);
  for (int k = 0; k < nc; k++) {
    int c = (pick + k) % nc;
    for (int j = 0; j < n; j++)
      if (rootMv[j].from == cf[c] && rootMv[j].to == ct[c]) { out = rootMv[j]; return true; }
  }
  return false;
}

bool botChoose(const Pos &p, const Lvl &lv, Move &out, bool variety) {
  int n = genLegal(p, rootMv);
  if (n == 0) return false;
  if (n == 1) { out = rootMv[0]; return true; }
  if (lv.rnd && random(100) < lv.rnd) { out = rootMv[random(n)]; return true; }
  if (lv.book && bookMove(lv.book, n, out)) return true;

  memset(killers, 0, sizeof(killers));
  for (int i = 0; i < n; i++) { R[i].m = rootMv[i]; R[i].s = orderScore(p, rootMv[i]); }
  sortRoot(n);

  int noise = lv.noise;
  if (variety && p.full <= 5 && noise < 40) noise = 40;   // variedade quando sai do livro
  int margin = (lv.errMargin > noise * 2 ? lv.errMargin : noise * 2) + 1;
  uint32_t t0 = millis();
  nodes = 0;

  for (int d = 1; d <= lv.depth; d++) {
    stopFlag = false;
    deadline = (d == 1) ? 0xFFFFFFFFu : t0 + lv.ms;
    rootLimit = d;
    int best = -INF;
    bool done = true;
    for (int i = 0; i < n; i++) {
      Pos c = p;
      doMove(c, R[i].m);
      int alpha = (best > -INF) ? best - margin : -INF;
      int v;
      if (repOf(c) >= 2) v = 0;                             // terceira repeticao = empate
      else {
        v = -negamax(c, d - 1, -INF, -alpha, 1);
        if (stopFlag) { done = false; break; }
      }
      if (v > alpha) { tmpS[i] = v; if (v > best) best = v; }
      else tmpS[i] = -INF;
    }
    if (!done) break;
    for (int i = 0; i < n; i++) R[i].s = tmpS[i];
    sortRoot(n);
    if (R[0].s > MATE - 200) break;
    if (millis() - t0 > lv.ms / 4) break;
  }

  int idx = 0;
  if (R[0].s <= MATE - 200) {
    int bestS = R[0].s;
    if (lv.err && (int)random(100) < lv.err) {
      // deslize "humano": escolhe entre lances pouco piores que o melhor
      int cand[64], nc = 0;
      for (int i = 0; i < n && nc < 64; i++)
        if (R[i].s > -INF && R[i].s >= bestS - lv.errMargin) cand[nc++] = i;
      idx = cand[random(nc)];
    } else {
      int bestVal = -100000;
      for (int i = 0; i < n; i++) {
        if (R[i].s <= -INF) continue;
        int val = R[i].s + (noise ? (int)random(-noise, noise + 1) : 0);
        if (val > bestVal) { bestVal = val; idx = i; }
      }
    }
  }
  out = R[idx].m;
  return true;
}

// ============================================================
//  Notacao (SAN) e leitura do que o usuario digita
// ============================================================
static void sqName(int sq, char *o) { o[0] = 'a' + (sq & 7); o[1] = '1' + (sq >> 3); }

// disp=true: formato bonito (Nxe5, exd8=Q).  disp=false: formato de comparacao (sem x, sem =)
void sanStr(const Pos &p, const Move &m, const Move *all, int n, char *o, bool disp) {
  int k = 0, pc = abs(p.b[m.from]);
  if (m.flags & F_CASTLE) {
    bool ks = (m.to & 7) == 6;
    strcpy(o, ks ? (disp ? "O-O" : "OO") : (disp ? "O-O-O" : "OOO"));
    return;
  }
  bool cap = (m.flags & (F_CAP | F_EP)) != 0;
  if (pc == 1) {
    if (cap) { o[k++] = 'a' + (m.from & 7); if (disp) o[k++] = 'x'; }
  } else {
    o[k++] = PCH[pc];
    bool amb = false, sf = false, sr = false;
    for (int j = 0; j < n; j++) {
      const Move &q = all[j];
      if (q.to == m.to && q.from != m.from && abs(p.b[q.from]) == pc) {
        amb = true;
        if ((q.from & 7) == (m.from & 7)) sf = true;
        if ((q.from >> 3) == (m.from >> 3)) sr = true;
      }
    }
    if (amb) {
      if (!sf) o[k++] = 'a' + (m.from & 7);
      else if (!sr) o[k++] = '1' + (m.from >> 3);
      else { o[k++] = 'a' + (m.from & 7); o[k++] = '1' + (m.from >> 3); }
    }
    if (cap && disp) o[k++] = 'x';
  }
  sqName(m.to, o + k); k += 2;
  if (m.promo) { if (disp) o[k++] = '='; o[k++] = PCH[m.promo]; }
  o[k] = 0;
}

// ============================================================
//  Estado global do jogo
// ============================================================
Move    legal[256];
int     nLegal = 0;

enum { ST_MENU, ST_GAME, ST_OVER };
int  state = ST_MENU;
int  optMode = 0;      // 0 = vs bot, 1 = 2 jogadores
int  optLevel = 3;     // 1..5
int  optColor = 0;     // 0 brancas, 1 pretas, 2 aleatorio
int  menuSel = 0;
uint8_t lang = 0;      // 0 = portugues, 1 = ingles
#define T(pt, en) (lang == 0 ? (pt) : (en))
int  humanSide = 0, botSide = 1;
bool flipB = false;
bool sndOn = true;

char inp[12]; int inLen = 0;
char msg1[24] = "", msg2[24] = "";
uint16_t msgCol;
uint16_t cLight, cDark, cHlL, cHlD, cRed, cGray, cDGray, cYellow, cGreen, cWhite, cBlack;

void beep(int f, int ms) { if (sndOn) M5Cardputer.Speaker.tone(f, ms); }

void setMsg(const char *a, const char *b, uint16_t col) {
  strncpy(msg1, a, 23); msg1[23] = 0;
  strncpy(msg2, b, 23); msg2[23] = 0;
  msgCol = col;
}

// ---------- Fim de jogo ----------
bool sameKey(const Pos &a, const Pos &b) {
  return a.side == b.side && a.castle == b.castle && a.ep == b.ep && memcmp(a.b, b.b, 64) == 0;
}
int repCount() {
  int c = 0;
  for (int i = hp; i >= 0; i--) {
    if (sameKey(H[i], H[hp])) c++;
    if (H[i].half == 0) break;
  }
  return c;
}
bool insufficient(const Pos &p) {
  int minors = 0;
  for (int i = 0; i < 64; i++) {
    int a = abs(p.b[i]);
    if (a == 1 || a == 4 || a == 5) return false;
    if (a == 2 || a == 3) minors++;
  }
  return minors <= 1;
}
bool checkEnd() {
  Pos &p = H[hp];
  const char *t = 0, *s = 0;
  if (nLegal == 0) {
    if (inCheck(p, p.side)) { t = T("XEQUE-MATE!", "CHECKMATE!"); s = p.side ? T("Brancas vencem", "White wins") : T("Pretas vencem", "Black wins"); }
    else { t = T("AFOGAMENTO", "STALEMATE"); s = T("Empate", "Draw"); }
  } else if (p.half >= 100)   { t = T("EMPATE", "DRAW"); s = T("Regra 50 lances", "50-move rule"); }
  else if (repCount() >= 3)   { t = T("EMPATE", "DRAW"); s = T("Repeticao x3", "3-fold repetition"); }
  else if (insufficient(p))   { t = T("EMPATE", "DRAW"); s = T("Mat. insuficiente", "Insufficient material"); }
  if (t) { state = ST_OVER; setMsg(t, s, cYellow); return true; }
  return false;
}

// ============================================================
//  Desenho
// ============================================================
#define SQ 15
#define BX 8

// Silhuetas 11x11: '#' = peca, 'o' = detalhe (olho/fenda), '.' = vazio
static const char *const SPR[6][11] = {
  { // peao
    "...........",
    "....###....",
    "...#####...",
    "....###....",
    ".....#.....",
    "....###....",
    ".....#.....",
    "....###....",
    "...#####...",
    "..#######..",
    "..#######.." },
  { // cavalo
    "....##.....",
    "...####....",
    "..######...",
    ".#o#####...",
    "########...",
    "#######....",
    "..######...",
    "....#####..",
    "...#######.",
    "..#########",
    "..#########" },
  { // bispo
    ".....#.....",
    "....###....",
    "...#####...",
    "...##o##...",
    "...#####...",
    "....###....",
    ".....#.....",
    "...#####...",
    "..#######..",
    "..#######..",
    ".#########." },
  { // torre
    "###.###.###",
    "###########",
    ".#########.",
    "..#######..",
    "...#####...",
    "...#####...",
    "...#####...",
    "...#####...",
    "..#######..",
    ".#########.",
    "###########" },
  { // dama
    "#.#.#.#.#.#",
    "###########",
    ".#########.",
    "..#######..",
    "...#####...",
    "....###....",
    "....###....",
    "...#####...",
    "..#######..",
    ".#########.",
    "###########" },
  { // rei
    ".....#.....",
    "....###....",
    ".....#.....",
    "..#######..",
    ".#########.",
    ".#########.",
    "..#######..",
    "...#####...",
    "..#######..",
    ".#########.",
    ".#########." }
};

M5Canvas *sqSprite = nullptr;   // sprite 15x15 para desenhar cada casa sem piscar

void drawSquare(int x, int y, uint16_t bg, int8_t pc) {
  M5Canvas &s = *sqSprite;
  s.fillSprite(bg);
  if (pc) {
    const char *const *spr = SPR[abs(pc) - 1];
    uint16_t fill = pc > 0 ? cWhite : LCD.color565(35, 35, 35);
    uint16_t line = pc > 0 ? cBlack : LCD.color565(225, 225, 225);
    const int ox = 2, oy = 2;
    for (int r = 0; r < 11; r++)
      for (int c = 0; c < 11; c++)
        if (spr[r][c] != '.')
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
              s.drawPixel(ox + c + dx, oy + r + dy, line);
    for (int r = 0; r < 11; r++)
      for (int c = 0; c < 11; c++) {
        if (spr[r][c] == '#') s.drawPixel(ox + c, oy + r, fill);
        else if (spr[r][c] == 'o') s.drawPixel(ox + c, oy + r, line);
      }
  }
  s.pushSprite(x, y);
}

void drawBoard() {
  Pos &p = H[hp];
  bool chk = inCheck(p, p.side);
  for (int r = 0; r < 8; r++) {
    for (int f = 0; f < 8; f++) {
      int col = flipB ? 7 - f : f;
      int row = flipB ? r : 7 - r;
      int x = BX + col * SQ, y = row * SQ;
      int sq = r * 8 + f;
      bool light = ((r + f) & 1) == 1;
      uint16_t c = light ? cLight : cDark;
      if (hp > 0 && (sq == LF[hp] || sq == LT[hp])) c = light ? cHlL : cHlD;
      if (chk && sq == p.ksq[p.side]) c = cRed;
      drawSquare(x, y, c, p.b[sq]);
    }
  }
  LCD.setTextSize(1);
  LCD.setTextColor(cGray, cBlack);
  for (int i = 0; i < 8; i++) {
    LCD.setCursor(BX + i * SQ + 5, 121);
    LCD.print((char)(flipB ? 'h' - i : 'a' + i));
    LCD.setCursor(1, i * SQ + 4);
    LCD.print((char)(flipB ? '1' + i : '8' - i));
  }
}

void drawPanel() {
  LCD.fillRect(130, 0, 110, 105, cBlack);
  LCD.setTextSize(1);
  Pos &p = H[hp];
  char b[28];

  LCD.setTextColor(cWhite);
  LCD.setCursor(130, 2);
  const char *who = "";
  if (optMode == 0) who = (p.side == botSide) ? T(" (bot)", " (bot)") : T(" (vc)", " (you)");
  snprintf(b, sizeof b, T("Vez: %s%s", "Turn: %s%s"), p.side ? T("Pretas", "Black") : T("Brancas", "White"), who);
  if (state == ST_OVER) snprintf(b, sizeof b, T("Fim de jogo", "Game over"));
  LCD.print(b);

  LCD.setTextColor(cGray);
  LCD.setCursor(130, 12);
  if (optMode == 0) snprintf(b, sizeof b, T("Bot N%d %s", "Bot L%d %s"), optLevel, LVN(optLevel - 1));
  else snprintf(b, sizeof b, T("2 Jogadores", "2 Players"));
  LCD.print(b);

  int first = hp - 4; if (first < 1) first = 1;
  int y = 24;
  for (int i = first; i <= hp; i++, y += 9) {
    const Pos &q = H[i - 1];
    if (q.side == 0) snprintf(b, sizeof b, "%d. %s", q.full, SAN[i]);
    else             snprintf(b, sizeof b, "%d...%s", q.full, SAN[i]);
    LCD.setTextColor(i == hp ? cWhite : cGray);
    LCD.setCursor(130, y);
    LCD.print(b);
  }

  LCD.setTextColor(msgCol);
  LCD.setCursor(130, 71); LCD.print(msg1);
  LCD.setCursor(130, 80); LCD.print(msg2);

  LCD.setTextColor(cDGray);
  LCD.setCursor(130, 91); LCD.print("menu  undo");
  LCD.setCursor(130, 99); LCD.print("flip  hint");
}

void drawInput() {
  LCD.fillRect(129, 106, 111, 29, cBlack);
  LCD.drawRect(129, 106, 111, 29, state == ST_OVER ? cYellow : cGray);
  LCD.setTextSize(2);
  if (state == ST_OVER) {
    LCD.setTextColor(cYellow);
    LCD.setCursor(135, 112);
    LCD.print("ENTER:menu");
    LCD.setTextSize(1);
    return;
  }
  LCD.setTextColor(cWhite);
  LCD.setCursor(134, 112);
  LCD.print(inp);
  if (inLen < 8) LCD.print("_");
  LCD.setTextSize(1);
}

void refresh() {
  LCD.fillScreen(cBlack);
  drawBoard();
  drawPanel();
  drawInput();
}

void drawMenu() {
  LCD.fillScreen(cBlack);
  LCD.setTextSize(2);
  LCD.setTextColor(cYellow);
  LCD.setCursor(8, 4);
  LCD.print(T("XADREZ", "CHESS"));
  LCD.setTextSize(1);
  LCD.setTextColor(cGray);
  LCD.setCursor(150, 10);
  LCD.print("Cardputer");

  static const char *modosPT[2] = {"vs Bot", "2 Jogad."};
  static const char *modosEN[2] = {"vs Bot", "2 Players"};
  static const char *coresPT[3] = {"Brancas", "Pretas", "Aleat."};
  static const char *coresEN[3] = {"White", "Black", "Random"};
  const char **modos = lang == 0 ? modosPT : modosEN;
  const char **cores = lang == 0 ? coresPT : coresEN;
  const char *langs[2] = {"PT-BR", "EN-US"};
  char b[28];
  LCD.setTextSize(2);
  for (int i = 0; i < 5; i++) {
    int y = 28 + i * 20;
    bool dis = (optMode == 1 && (i == 1 || i == 2));
    switch (i) {
      case 0: snprintf(b, sizeof b, T("Modo   %s", "Mode   %s"), modos[optMode]); break;
      case 1: snprintf(b, sizeof b, T("Nivel  %d %s", "Level  %d %s"), optLevel, LVN(optLevel - 1)); break;
      case 2: snprintf(b, sizeof b, T("Voce   %s", "You    %s"), cores[optColor]); break;
      case 3: snprintf(b, sizeof b, T("Idioma %s", "Lang   %s"), langs[lang]); break;
      default: snprintf(b, sizeof b, T("INICIAR", "START")); break;
    }
    if (i == menuSel) { LCD.setTextColor(cYellow); LCD.setCursor(4, y); LCD.print(">"); }
    LCD.setTextColor(dis ? cDGray : (i == menuSel ? cWhite : cGray));
    LCD.setCursor(24, y);
    LCD.print(b);
  }
  LCD.setTextSize(1);
  LCD.setTextColor(cGray);
  LCD.setCursor(6, 124);
  LCD.print(T("; . move   , / muda   Enter ok", "; . move   , / change   Enter ok"));
}

// ============================================================
//  Fluxo do jogo
// ============================================================
void startGame() {
  initPos(H[0]);
  ttClear();
  hp = 0; LF[0] = LT[0] = 255; SAN[0][0] = 0;
  humanSide = (optColor == 2) ? (int)random(2) : optColor;
  botSide = 1 - humanSide;
  flipB = (optMode == 0 && humanSide == 1);
  inLen = 0; inp[0] = 0;
  nLegal = genLegal(H[0], legal);
  state = ST_GAME;
  if (optMode == 0) setMsg(humanSide ? T("Voce: Pretas", "You: Black") : T("Voce: Brancas", "You: White"), T("ex: e4 Nf3 O-O", "e.g.: e4 Nf3 O-O"), cGreen);
  else setMsg(T("Digite o lance", "Enter your move"), T("ex: e4 Nf3 O-O", "e.g.: e4 Nf3 O-O"), cGreen);
  refresh();
}

void applyMove(const Move &m, bool byBot) {
  char s[12];
  sanStr(H[hp], m, legal, nLegal, s, true);
  Pos nx = H[hp];
  doMove(nx, m);
  bool chk = attacked(nx, nx.ksq[nx.side], nx.side ^ 1);

  if (hp >= HMAX - 1) {            // descarta o inicio do historico
    const int SH = 100;
    memmove(H,   H + SH,   (HMAX - SH) * sizeof(Pos));
    memmove(SAN, SAN + SH, (HMAX - SH) * sizeof(SAN[0]));
    memmove(LF,  LF + SH,  (HMAX - SH));
    memmove(LT,  LT + SH,  (HMAX - SH));
    hp -= SH;
  }
  hp++;
  H[hp] = nx; LF[hp] = m.from; LT[hp] = m.to;
  nLegal = genLegal(nx, legal);
  if (chk) { size_t l = strlen(s); s[l] = nLegal ? '+' : '#'; s[l + 1] = 0; }
  strncpy(SAN[hp], s, 9); SAN[hp][9] = 0;

  char l1[24] = "";
  if (byBot) snprintf(l1, sizeof l1, T("Bot jogou: %s", "Bot played: %s"), s);
  setMsg(l1, chk ? T("Xeque!", "Check!") : "", chk ? cRed : cGreen);
  if (!checkEnd()) beep(chk ? 1500 : 1000, 40);
  else beep(600, 300);
}

void botTurn() {
  setMsg(T("Bot pensando...", "Bot thinking..."), "", cYellow);
  drawPanel();
  Move m;
  if (botChoose(H[hp], LV[optLevel - 1], m, true)) applyMove(m, true);
  refresh();
}

// ---------- Interpretacao do texto digitado ----------
// retorna 1 = ok, 0 = invalido, -1 = ambiguo
int parseMove(const char *in, Move &out) {
  Pos &p = H[hp];
  char t[12], cs[8];

  // roque: O-O, 0-0, o-o ...
  bool onlyC = true; int zc = 0;
  for (const char *q = in; *q; q++) {
    if (*q == '0' || *q == 'o' || *q == 'O') zc++;
    else if (*q != '-') onlyC = false;
  }
  if (onlyC && (zc == 2 || zc == 3)) {
    int tf = (zc == 3) ? 2 : 6;
    for (int i = 0; i < nLegal; i++)
      if ((legal[i].flags & F_CASTLE) && (legal[i].to & 7) == tf) { out = legal[i]; return 1; }
    return 0;
  }

  // normaliza: tira x - + # = ! ?
  char s[12]; int k = 0;
  for (const char *q = in; *q && k < 10; q++) {
    char c = *q;
    if (c == 'x' || c == 'X' || c == '-' || c == '+' || c == '#' || c == '=' || c == '!' || c == '?') continue;
    s[k++] = c;
  }
  s[k] = 0;
  if (k < 2) return 0;

  auto matchExact = [&](const char *str, bool coord) -> int {
    int cnt = 0, found = -1;
    for (int i = 0; i < nLegal; i++) {
      sanStr(p, legal[i], legal, nLegal, t, false);
      bool ok = strcmp(t, str) == 0;
      if (!ok && coord && strlen(str) >= 4) {
        sqName(legal[i].from, cs); sqName(legal[i].to, cs + 2);
        if (legal[i].promo) { cs[4] = tolower(PCH[legal[i].promo]); cs[5] = 0; } else cs[4] = 0;
        ok = strcasecmp(cs, str) == 0;
      }
      if (ok) { cnt++; found = i; }
    }
    if (cnt == 1) { out = legal[found]; return 1; }
    return cnt > 1 ? -1 : 0;
  };

  int r = matchExact(s, true);
  if (r) return r;

  // ajusta maiusculas: nf3 -> Nf3, e8q -> e8Q
  char s2[12]; strcpy(s2, s);
  if (strchr("nbrqk", s2[0])) s2[0] = toupper(s2[0]);
  int L = strlen(s2);
  if (L >= 3 && strchr("nbrq", s2[L - 1]) && (s2[L - 2] == '1' || s2[L - 2] == '8')) s2[L - 1] = toupper(s2[L - 1]);
  if (strcmp(s, s2) != 0) { r = matchExact(s2, false); if (r) return r; }

  // peca com origem a mais ou a menos (Ngf3 quando nao precisa, Nd7 quando e ambiguo)
  if (strchr("NBRQK", s2[0])) {
    int pc = strchr(PCH, s2[0]) - PCH;
    const char *rest = s2 + 1;
    int rl = strlen(rest);
    if (rl >= 2 && rest[rl - 2] >= 'a' && rest[rl - 2] <= 'h' && rest[rl - 1] >= '1' && rest[rl - 1] <= '8' && rl <= 4) {
      int dest = (rest[rl - 1] - '1') * 8 + (rest[rl - 2] - 'a');
      int cnt = 0, found = -1;
      for (int i = 0; i < nLegal; i++) {
        if (legal[i].to != dest || abs(p.b[legal[i].from]) != pc || (legal[i].flags & F_CASTLE)) continue;
        bool ok = true;
        for (int h = 0; h < rl - 2 && ok; h++) {
          char c = rest[h];
          if (c >= 'a' && c <= 'h') ok = ((legal[i].from & 7) == c - 'a');
          else if (c >= '1' && c <= '8') ok = ((legal[i].from >> 3) == c - '1');
          else ok = false;
        }
        if (ok) { cnt++; found = i; }
      }
      if (cnt == 1) { out = legal[found]; return 1; }
      if (cnt > 1) return -1;
    }
    return 0;
  }

  // promocao sem peca informada -> Dama automatica
  char s3[14]; snprintf(s3, sizeof s3, "%sQ", s);
  r = matchExact(s3, false);
  if (r == 1) return 1;
  return 0;
}

void doUndo() {
  int minHp = (optMode == 0 && botSide == 0) ? 1 : 0;
  int step = (optMode == 0) ? 2 : 1;
  if (hp - step < minHp) { setMsg(T("Nada para", "Nothing to"), T("desfazer", "undo"), cRed); return; }
  hp -= step;
  nLegal = genLegal(H[hp], legal);
  setMsg(T("Lance desfeito", "Move undone"), "", cGreen);
}

void doHint() {
  setMsg(T("Pensando...", "Thinking..."), "", cYellow);
  drawPanel();
  Move m;
  Lvl hl = {6, 6000, 0, 0, 0, 0, 24};
  if (botChoose(H[hp], hl, m, false)) {
    char s[12];
    sanStr(H[hp], m, legal, nLegal, s, true);
    setMsg(T("Dica:", "Hint:"), s, cGreen);
  }
}

void submit() {
  if (inLen == 0) return;
  char c[12];
  for (int i = 0; i <= inLen; i++) c[i] = tolower(inp[i]);

  if (!strcmp(c, "menu") || !strcmp(c, "m")) { state = ST_MENU; drawMenu(); return; }
  if (!strcmp(c, "undo") || !strcmp(c, "u")) { doUndo(); inLen = 0; inp[0] = 0; refresh(); return; }
  if (!strcmp(c, "flip") || !strcmp(c, "f")) { flipB = !flipB; inLen = 0; inp[0] = 0; refresh(); return; }
  if (!strcmp(c, "hint") || !strcmp(c, "h")) { doHint(); inLen = 0; inp[0] = 0; refresh(); return; }
  if (!strcmp(c, "snd")) { sndOn = !sndOn; setMsg(sndOn ? T("Som ligado", "Sound on") : T("Som desligado", "Sound off"), "", cGreen); inLen = 0; inp[0] = 0; drawPanel(); drawInput(); return; }

  Move m;
  int r = parseMove(inp, m);
  if (r == 1) {
    inLen = 0; inp[0] = 0;
    applyMove(m, false);
    refresh();
  } else {
    if (r == -1) setMsg(T("Ambiguo!", "Ambiguous!"), T("informe a origem", "specify the origin"), cRed);
    else setMsg(T("Lance invalido", "Invalid move"), T("ex: Nf3 e4 O-O", "e.g.: Nf3 e4 O-O"), cRed);
    beep(300, 120);
    drawPanel();
    drawInput();
  }
}

// ============================================================
//  Menu
// ============================================================
void menuChange(int dir) {
  switch (menuSel) {
    case 0: optMode ^= 1; break;
    case 1: optLevel += dir; if (optLevel > 5) optLevel = 1; if (optLevel < 1) optLevel = 5; break;
    case 2: optColor = (optColor + dir + 3) % 3; break;
    case 3: lang ^= 1; break;
  }
}

void handleMenu(const Keyboard_Class::KeysState &st) {
  for (char c : st.word) {
    if (c == ';') menuSel = (menuSel + 4) % 5;
    else if (c == '.') menuSel = (menuSel + 1) % 5;
    else if (c == ',') menuChange(-1);
    else if (c == '/') menuChange(1);
  }
  if (st.enter) {
    if (menuSel == 4) { startGame(); return; }
    menuChange(1);
  }
  drawMenu();
}

void handleGame(const Keyboard_Class::KeysState &st) {
  if (st.del && inLen > 0) { inp[--inLen] = 0; }
  for (char c : st.word) {
    if (inLen < 8 && (isalnum((unsigned char)c) || c == '-' || c == '=' || c == '+' || c == '#')) {
      inp[inLen++] = c; inp[inLen] = 0;
    }
  }
  if (st.enter) { submit(); return; }
  drawInput();
}

// ============================================================
//  Arduino
// ============================================================
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  LCD.setRotation(1);
  LCD.setTextWrap(false);
  sqSprite = new M5Canvas(&LCD);
  sqSprite->setColorDepth(16);
  sqSprite->createSprite(SQ, SQ);
  M5Cardputer.Speaker.setVolume(90);
  randomSeed(esp_random());
  initZobrist();
  ttInit();

  cBlack  = 0x0000; cWhite = 0xFFFF;
  cLight  = LCD.color565(240, 217, 181);
  cDark   = LCD.color565(181, 136, 99);
  cHlL    = LCD.color565(205, 210, 106);
  cHlD    = LCD.color565(170, 162, 58);
  cRed    = LCD.color565(200, 60, 60);
  cGray   = LCD.color565(160, 160, 160);
  cDGray  = LCD.color565(80, 80, 80);
  cYellow = LCD.color565(255, 215, 0);
  cGreen  = LCD.color565(110, 210, 120);
  msgCol  = cGreen;
  inp[0] = 0;

  state = ST_MENU;
  drawMenu();
}

void loop() {
  M5Cardputer.update();

  if (state == ST_GAME && optMode == 0 && H[hp].side == botSide) {
    botTurn();
    return;
  }

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    if (state == ST_MENU) handleMenu(st);
    else if (state == ST_GAME) handleGame(st);
    else if (state == ST_OVER && st.enter) { state = ST_MENU; drawMenu(); }
  }
  delay(5);
}
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif
