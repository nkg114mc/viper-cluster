#include "viper.h"
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

int VSQ[64] = {
A1, B1, C1, D1, E1, F1, G1, H1,
A2, B2, C2, D2, E2, F2, G2, H2,
A3, B3, C3, D3, E3, F3, G3, H3,
A4, B4, C4, D4, E4, F4, G4, H4,
A5, B5, C5, D5, E5, F5, G5, H5,
A6, B6, C6, D6, E6, F6, G6, H6,
A7, B7, C7, D7, E7, F7, G7, H7,
A8, B8, C8, D8, E8, F8, G8, H8};

void init_position(position_t *pos) {
  int i;
  for(i=0; i<256; i++) pos->board_[i] = ((i-64)&0x88)? OUTSIDE : EMPTY;
  pos->board = pos->board_ + 64;
}

void init_piece_lists(position_t *pos) {
  int sq, piece;

  for(piece = KING; piece >= KNIGHT; piece--) {
    PieceListStart(pos, piece) = piece-1+128;
    PieceListStart(pos, piece+8) = piece+7+128;
    PrevPiece(pos, piece-1+128) = piece+128; 
    PrevPiece(pos, piece+7+128) = piece+8+128;
  }
  PieceListStart(pos, WP) = PieceListStart(pos, BP) = PieceListEnd;

  for(sq = A1; sq <= H8; sq++) 
    if(pos->board[sq] != OUTSIDE && pos->board[sq] != EMPTY) 
      InsertPiece(pos, pos->board[sq], sq);
}

void copy_position(position_t *dst, const position_t *src) {
  memcpy(dst, src, sizeof(position_t));
  dst->board = dst->board_ + 64;
}

bool is_attacked(const position_t *pos, int square, int side) {
  int sq, tosq, piece, step;
  attack_data_t *a = AttackData-square;

  for(sq = KingSquare(pos, side); sq != PieceListEnd; 
      sq = NextPiece(pos, sq)) {
    if(sq <= H8) {
      piece = pos->board[sq];
      if(PieceMask[piece] & a[sq].may_attack) {
	if(!SLIDER(piece)) return true;
	step = a[sq].step;
	for(tosq=sq+step; pos->board[tosq]==EMPTY&&tosq!=square; tosq+=step);
	if(tosq == square) return true;
      }
    }
  }
  return false;
}

bool position_is_check(const position_t *pos) {
  int us, them;
  move_t move;

  us = pos->side; them = us^1;
  move = pos->last_move;

  if(move == NULLMOVE) return false;
  else if(move == NOMOVE || CASTLING(move) || EP(move))
    return is_attacked(pos, KingSquare(pos, us), them);
  else {
    int ksq = KingSquare(pos, us);
    int from = FROM(move), to = PIECE(move);
    int piece = pos->board[to];
    attack_data_t *a = AttackData - ksq;
    if(a[to].may_attack & PieceMask[piece]) {
      if(!PieceIsSlider(piece)) return true;
      int step = a[to].step, sq;
      for(sq = to + step; pos->board[sq] == EMPTY; sq += step);
      if(sq == ksq) return true;
    }
    if(a[from].may_attack & Q_MASK) {
      int step = a[from].step, sq;
      for(sq = from + step; pos->board[sq] == EMPTY; sq += step);
      if(sq == ksq) {
	for(sq = from - step; pos->board[sq] == EMPTY; sq -= step);
	if(ColourOfPiece(pos->board[sq]) == them &&
	   (a[sq].may_attack & PieceMask[pos->board[sq]]))
	  return true;
      }
    }
  }
  return false;
}

int find_checkers(const position_t *pos, int chsqs[]) {
  int us = pos->side, them = us^1;
  int ksq = KingSquare(pos, us), from, to, step, piece, result = 0;
  move_t move = pos->last_move;
  attack_data_t *a = AttackData - ksq;

  if(move == NULLMOVE) return 0;
  chsqs[0] = chsqs[1] = 0;
  if(move == NOMOVE || CASTLING(move) || EP(move) || PROMOTION(move)) {
    for(from = KingSquare(pos, pos->side^1); from != PieceListEnd && result<2; 
	from = NextPiece(pos, from)) {
      if(from > H8) continue;
      piece = pos->board[from];
      if(PieceMask[piece] & a[from].may_attack) {
	if(SLIDER(piece)) {
	  step = a[from].step;
	  for(to = from + step; pos->board[to] == EMPTY; to += step);
	  if(to == ksq) chsqs[result++] = from;
	}
	else chsqs[result++] = from;
      }
    }
  }
  else {
    from = FROM(move); to = TO(move);
    piece = pos->board[to];
    if(PieceMask[piece] & a[to].may_attack) {
      if(SLIDER(piece)) {
	int sq;
	step = a[to].step;
	for(sq = to + step; pos->board[sq]==EMPTY; sq += step);
	if(sq == ksq) chsqs[result++] = to;
      }
      else chsqs[result++] = to;
    }
    if(a[from].may_attack & Q_MASK) { // Discovered check possible.
      int sq;
      step = a[from].step;
      for(sq = from + step; pos->board[sq] == EMPTY; sq += step);
      if(sq == ksq) {
	for(sq = from - step; pos->board[sq] == EMPTY; sq -= step);
	if(ColourOfPiece(pos->board[sq]) == them && 
	   (a[sq].may_attack & PieceMask[pos->board[sq]]))
	  chsqs[result++] = sq;
      }
    }
  }
  return result;
}

int is_pinned(const position_t *pos, int square) {
  int side, ksq, p1, p2, step, sq;
  attack_data_t *a;

  side = ColourOfPiece(pos->board[square]);
  ksq = KingSquare(pos, side);

  a = AttackData - ksq + square;
  if(!(a->may_attack & Q_MASK)) return 0;

  if(a->may_attack & R_MASK) p1 = RookOfColour(side^1);
  else p1 = BishopOfColour(side^1);
  p2 = QueenOfColour(side^1);

  step = a->step;
  for(sq = square + step; pos->board[sq] == EMPTY; sq += step);
  if(sq == ksq) {
    for(sq = square - step; pos->board[sq] == EMPTY; sq -= step);
    if(pos->board[sq] == p1 || pos->board[sq] == p2) return step;
  }
  return 0;
}
  
int is_disc_check_candidate(const position_t *pos, int square) {
  int side, ksq, p1, p2, step, sq;
  attack_data_t *a;

  side = ColourOfPiece(pos->board[square]);
  ksq = KingSquare(pos, side^1);

  a = AttackData - ksq + square;
  if(!(a->may_attack & Q_MASK)) return 0;

  if(a->may_attack & R_MASK) p1 = RookOfColour(side);
  else p1 = BishopOfColour(side);
  p2 = QueenOfColour(side);

  step = a->step;
  for(sq = square + step; pos->board[sq] == EMPTY; sq += step);
  if(sq == ksq) {
    for(sq = square - step; pos->board[sq] == EMPTY; sq -= step);
    if(pos->board[sq] == p1 || pos->board[sq] == p2) return step;
  }
  return 0;
}

int count_pieces(const position_t *pos, int colour, int type) {
  int piece, square, count = 0;
  piece = PieceOfColourAndType(colour, type);
  for(square = PieceListStart(pos, piece); square <= H8; 
      square = NextPiece(pos, square))
    count++;
  return count;
}

void init_piece_counts(position_t *pos) {
  int colour, type;
  for(colour = WHITE; colour <= BLACK; colour++)
    for(type = PAWN; type <= QUEEN; type++)
      pos->piece_count[colour][type] = count_pieces(pos, colour, type);
}

string num2str(int i)
{
  stringstream ss;
  ss << i;
  return ss.str();
}


void position_to_fen(const position_t *pos, char *fen) {
  int i, j, k, sq, vsq, space, piece, bptr;
  char boardstr[256], side[32], KRSQ[32], epsq[32], rule50[32], fullmove[32];
  char alphapiece[33] = ".PNBRQK..pnbrqk..............\0";
  char fenstr[256];
  string str;



  // board
  bptr = 0; space = 0;
  for (i = 7; i >= 0; i--) {
    space = 0;
    for (j = 0; j < 8; j++) {
      sq = i * 8 + j;
      vsq = VSQ[sq];
      piece = pos->board[vsq];
      if (piece != EMPTY && piece != OUTSIDE) {
        if (space) {
          boardstr[bptr] = (char)(space + 48);
	  bptr++;
        }
        boardstr[bptr] = alphapiece[piece];
        bptr++;
        space = 0;
      } else {
        space++;
      }
    }

    if (space) {
          boardstr[bptr] = (char)(space + 48);
	  bptr++;
    }
    if (i > 0) {
	boardstr[bptr]  = '/';
	bptr++;
    }
  }
  boardstr[bptr] = '\0';

  // side
  if (pos->side == WHITE) {
    strcpy(side, "w\0");
  } else if (pos->side == BLACK) {
    strcpy(side, "b\0");
  }

  // KRSQ
  str = ""; i = 0;
  j = (W_OO_MASK | W_OOO_MASK | B_OO_MASK | B_OOO_MASK);
  k = pos->castle_flags & j;
  if (k == j) {
     KRSQ[0] = '-';
     KRSQ[1] = '\0';
  } else {
     if ((k & W_OO_MASK) == 0) {
        KRSQ[i] = 'K';
        i++;
     }
     if ((k & W_OOO_MASK) == 0) {
        KRSQ[i] = 'Q';
        i++;
     }
     if ((k & B_OO_MASK) == 0) {
        KRSQ[i] = 'k';
        i++;
     }
     if ((k & B_OOO_MASK) == 0) {
        KRSQ[i] = 'q';
        i++;
     }
     KRSQ[i] = '\0';
  }

  // epsq
  if (pos->ep_square != 0) {
    i = pos->ep_square % 16;
    j = pos->ep_square / 16;
    epsq[0] = (char)(i + (int)'a');
    epsq[1] = (char)(j + (int)'1');
    epsq[2] = '\0';
  } else {
    epsq[0] = '-';
    epsq[1] = '\0';
  }

  // rule50
  str = "";
  str = num2str(pos->rule50);
  strcpy(rule50, str.c_str());

  // fullmove
  str = "";
  i = 2;//(pos->gply) / 2;
  str = num2str(i);
  strcpy(fullmove, str.c_str());

  // fen string
  for (i = 0 ; i < 256; i++) fenstr[i] = '\0';
  sprintf(fenstr, "%s %s %s %s %s %s", boardstr, side, KRSQ, epsq, rule50, fullmove);
  strcpy(fen, fenstr);
  for (i = 0 ; i < 256; i++) {
    fen[i] = fenstr[i];
    if (fenstr[i] == '\0') break;
  }
}
