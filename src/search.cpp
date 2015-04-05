#include "viper.h"
#include <inttypes.h>


#define HINDEX2(pos, x) (COMPRESS(TO(x))|((pos->board[FROM(x)]-1)<<6))
#define HINDEX3(pos, x) (COMPRESS(TO(x))|((pos->board[TO(x)]-1)<<6))

struct move_stats_t {
  int success, failure;
};

static move_stats_t MoveStats[64*BK];

void init_move_stats(void) {
  memset(MoveStats, 0, 64*BK*sizeof(move_stats_t));
}

static void success(const position_t *pos, move_t m) {
  MoveStats[HINDEX2(pos, m)].success++;
}

static void failure(const position_t *pos, move_t m) {
  MoveStats[HINDEX2(pos, m)].failure++;
}

static int success_count(const position_t *pos, move_t m) {
  return MoveStats[HINDEX3(pos, m)].success;
}

static int failure_count(const position_t *pos, move_t m) {
  return MoveStats[HINDEX3(pos, m)].failure;
}

static void inc_history(search_stack_t *ss, int ply, move_t m, int side, 
			int depth, int value) {
  if(!CAPTURE(m) && !PROMOTION(m)) {
    HISTORY(side, m) += (depth>>5);
    ss[ply].killer2 = ss[ply].killer; ss[ply].killer = m;
  }
}

static void check_for_timeout(void) {
  int t;
  static int last_info_time;

  t = get_time() - RSI->start_time;
  if(t<1000) last_info_time = 0;
  else if(t-last_info_time>=1000) {
    mutex_lock(IOLock);
    printf("info nodes " llu_format " nps " llu_format " time %d\n",
           RSI->nodes, (RSI->nodes*1000ULL)/((uint64) t), t);
    mutex_unlock(IOLock);
    last_info_time = t;
  }
  if(RSI->thinking_status != PONDERING && RSI->iteration >= 2) {
    if(!RSI->infinite && 
       (t > RSI->absolute_max_time
        || (RSI->root_moves.current == 1 && t > RSI->max_time) 
        || (!RSI->fail_high && t > 6 * RSI->max_time)))
      RSI->thinking_status = ABORTED;
    if(RSI->node_limit && RSI->nodes >= RSI->node_limit)
      RSI->thinking_status = ABORTED;
    if(RSI->exact_time && t >= RSI->exact_time)
      RSI->thinking_status = ABORTED;
  }
}
    
void init_node(const position_t *pos, search_stack_t *sstack, int ply,
	       int thread_id) {
  search_stack_t *ss = sstack+ply;
  RSI->nodes++; RSI->nodes_since_poll++;
  Threads[thread_id].nodes++;
  if(RSI->nodes_since_poll >= RSI->nodes_between_polls && thread_id == 0) {
    check_for_input();
    check_for_timeout();
    RSI->nodes_since_poll = 0;
  }

  ss->pv[ply] = ss->move = NOMOVE;
  (ss+1)->pv[ply+1] = NOMOVE;
  (ss+2)->killer = (ss+2)->killer2 = NOMOVE;
  ss->check = pos->check;
}

void update_pv(search_stack_t *sstack, int ply) {
  search_stack_t *ss = sstack+ply;
  int i;
  ss->pv[ply] = ss->move;
  for(i=ply+1; (ss+1)->pv[i] != NOMOVE; i++) ss->pv[i] = (ss+1)->pv[i];
  ss->pv[i] = NOMOVE;
}

int extend(const position_t *pos, search_stack_t *ss, int mate_threat) {
  int extension = 0;
  move_t m = ss->move;
  extension = 0;
  if(pos->check) extension += 60;
  if(mate_threat) extension += 45;
  if(PIECE(m)==PAWN && PawnRank[pos->xside][TO(m)] == RANK_7) 
    extension += 45;
  if(extension > PLY) extension = PLY;
  return extension;
}

static bool ok_to_reduce(const position_t *pos, const search_stack_t *ss, 
			 bool pvnode, int ply, move_t m) {
  if(PROMOTION(m) || CAPTURE(m)) return false;
  if(success_count(pos, m) > 2*failure_count(pos, m)) return false;
  return true;
}

int QNodes[MaxNumOfThreads];

int search(position_t *pos, search_stack_t *sstack, int ply, 
	   int alpha, int beta, int depth, int follow_pv, int nullmove,
	   int thread_id) {
  int bestvalue, value, nullvalue, legal_moves=0, mate_threat=0, ext;
  int old_alpha = alpha, old_beta = beta;
  bool pvnode = (beta - alpha > 1);
  int side = pos->side;
  move_t move, pvmove, hashmove;
  move_t searched_moves[256];
  move_stack_t *ms;
  undo_info_t u[1];
  search_stack_t *ss = sstack+ply;

  ss->pv[ply] = NOMOVE;
  if(RSI->thinking_status == ABORTED || Threads[thread_id].stop) return 0;
  if(alpha > MATE_VALUE - ply - 1) return alpha;

  if(ply >= 60) return evaluate(pos, thread_id);

  if(depth < PLY) {
    QNodes[thread_id] = 0;
    return qsearch(pos, sstack, ply, alpha, beta, 0, follow_pv, thread_id);
  }
  if(draw(pos)) return DRAW_VALUE;

  retrieve_tt(pos, &alpha, &beta, depth, &hashmove, &mate_threat, &nullmove);
  if(pvnode) {
    alpha = old_alpha; beta = old_beta;
  }
  if(alpha >= beta) {
    ss->move = hashmove; return alpha;
  }
  if(follow_pv) pvmove = sstack[0].pv[ply]; else pvmove = NOMOVE;
  if(hashmove && pvmove == NOMOVE) pvmove = hashmove;

  init_node(pos, sstack, ply, thread_id);

  // Null move search
  if(nullmove && !ss->check && 
     pos->material[WHITE] > 0 && pos->material[BLACK] > 0 
     && !pawn_endgame(pos) && !(pos->piece_count[side][PAWN] == 0 &&
                                pos->material[side] <= R_VALUE)) {
    make_nullmove(pos, u); 
    ss->move = NULLMOVE;
    (ss+1)->ms_ptr = ss->ms_ptr; 
    nullvalue = -search(pos, sstack, ply+1, -beta, -beta+1, 
			depth-4*PLY, 0, 0, thread_id);
    unmake_nullmove(pos, u);
    if(nullvalue >= beta) return nullvalue;
  }

  // Internal iterative deepening:
  if(pvmove == NOMOVE && ((pvnode && depth > 3*PLY) || depth > 6*PLY)) {
    value = search(pos, sstack, ply, alpha, beta, depth-2*PLY, 0, 0, thread_id);
    if(value >= max(beta, MATE_VALUE-100)) return value;
    if(value < min(alpha, -MATE_VALUE+100)) return value;
    pvmove = sstack[ply].pv[ply];
  }

  if(ss->check) {
    (ss+1)->ms_ptr = generate_check_evasions(pos, ss->ms_ptr);
    ss->num_of_evasions = (ss+1)->ms_ptr - ss->ms_ptr;
    if(ss->num_of_evasions == 0) return -MATE_VALUE+ply;
  }
  else (ss+1)->ms_ptr = generate_moves(pos, ss->ms_ptr);

  order_moves(pos, sstack, ply, pvmove); ms = ss->ms_ptr;

  bestvalue = -MATE_VALUE-1;

  while(alpha < beta && !Threads[thread_id].stop &&
	(move = pick_move(&ms, (ss+1)->ms_ptr, legal_moves < 16))) {
    if(move_is_legal(pos, move)) {
      searched_moves[legal_moves] = move;
      legal_moves++;
      
      ss->move = move;
      make_move(pos, move, u);
      ext = extend(pos, ss, mate_threat);

      if(legal_moves == 1) 
	value = -search(pos, sstack, ply+1, -beta, -alpha, depth-PLY+ext, 
			follow_pv, 1, thread_id);
      else {
	if(!ext && legal_moves >= 4 && depth >= 2*PLY && 
	   ok_to_reduce(pos, sstack, pvnode, ply, move)) {
	  value = -search(pos, sstack, ply+1, -alpha-1, -alpha, depth-2*PLY, 
			  0, 1, thread_id);
	}
	else value = alpha+1; // HACK
	if(value > alpha) {
	  value = -search(pos, sstack, ply+1, -alpha-1, -alpha, depth-PLY+ext,
			  0, 1, thread_id);
	  if(value > alpha && value < beta) 
	    value = -search(pos, sstack, ply+1, -beta, -alpha, depth-PLY+ext,
			    0, 1, thread_id);
	}
      }
      unmake_move(pos, move, u);
      if(value > bestvalue) {
	bestvalue = value;
	if(value > alpha) {
	  alpha = value; update_pv(sstack, ply);
	  if(value >= beta) inc_history(sstack, ply, move, side, depth, value);
	}
      }
    }
    
#if defined(SMP)
    // SMP: Use the "Young Brothers Wait" algorithm. If the first move has 
    // been searched and the other thread is idle, search the remaining
    // moves in parallel:
    if(Options->threads > 1 && legal_moves >= 1 && alpha < beta &&
       depth >= Options->split_depth*PLY && !mate_threat && 
       idle_thread_exists(thread_id) &&
       split(pos, sstack, ply, &alpha, &beta, pvnode, &bestvalue, depth, 
	     &legal_moves, ms, (ss+1)->ms_ptr, thread_id))
      break;
#endif
  }
  if(legal_moves == 0) return DRAW_VALUE;

  if(RSI->thinking_status == ABORTED || Threads[thread_id].stop) return 0;

  if(bestvalue >= beta) {
    store_tt(pos, bestvalue, depth, ss->pv[ply], LOWER_BOUND, mate_threat);
    if(!CAPTURE(ss->pv[ply]) && !PROMOTION(ss->pv[ply])) {
      int i;
      success(pos, ss->pv[ply]);
      for(i = 0; i < legal_moves - 1; i++)
	if(!CAPTURE(searched_moves[i]) && !PROMOTION(searched_moves[i]))
	  failure(pos, searched_moves[i]);
    }
  }
  else if(alpha == old_alpha) 
    store_tt(pos, bestvalue, depth, 0, UPPER_BOUND, mate_threat);
  else 
    store_tt(pos, bestvalue, depth, ss->pv[ply], EXACT, mate_threat);

  return alpha;
}

int qsearch(position_t *pos, search_stack_t *sstack, int ply, 
	    int alpha, int beta, int depth, int follow_pv, int thread_id) {
  int eval, bestvalue, value, moves=0;
  bool search_checks;
  move_t move, pvmove;
  move_stack_t *ms;
  undo_info_t u[1];
  search_stack_t *ss = sstack+ply;

  ss->pv[ply] = NOMOVE;
  if(RSI->thinking_status == ABORTED || Threads[thread_id].stop) return 0;

  if(alpha > MATE_VALUE - ply - 1) return alpha;

  QNodes[thread_id]++;
  if(ply >= 60) return evaluate(pos, thread_id);
  if(draw(pos)) return DRAW_VALUE;

  if(depth == 0 && QNodes[thread_id] < 400 && alpha < 2*N_VALUE)
    search_checks = true;
  else
    search_checks = false;

  if(follow_pv) pvmove = sstack[0].pv[ply]; else pvmove = NOMOVE;

  init_node(pos, sstack, ply, thread_id);
  eval = evaluate(pos, thread_id); 

  if(ss->check) {
    (ss+1)->ms_ptr = generate_check_evasions(pos, ss->ms_ptr);
    ss->num_of_evasions = (ss+1)->ms_ptr - ss->ms_ptr;
    if(ss->num_of_evasions == 0) return -MATE_VALUE+ply;
    bestvalue = -MATE_VALUE-1;
  }
  else {
    if(eval >= beta) return eval;
    if(eval > alpha) alpha = eval;
    (ss+1)->ms_ptr = generate_captures(pos, ss->ms_ptr);
    bestvalue = eval;
  }
  order_qmoves(pos, sstack, ply, pvmove); ms = ss->ms_ptr;

  while(alpha < beta && !Threads[thread_id].stop &&
	(move = pick_move(&ms, (ss+1)->ms_ptr, moves < 4))) 
    if(move_is_legal(pos, move)) {
      if(!ss->check && PieceValues[PIECE(move)] > PieceValues[CAPTURE(move)] 
         && see(pos, FROM(move), TO(move)) < 0) 
        continue;
      moves++;

      ss->move = move;
      make_move(pos, move, u);
      value = -qsearch(pos, sstack, ply+1, -beta, -alpha, depth-PLY, 
		       (moves==1)? follow_pv : 0, thread_id);
      unmake_move(pos, move, u);
      if(value > bestvalue) {
        bestvalue = value;
        if(value > alpha) {
          alpha = value; update_pv(sstack, ply);
        }
      }
    }
  if(search_checks && alpha < 2*N_VALUE && alpha < beta && !(ss->check)) {
    (ss+1)->ms_ptr = generate_checks(pos, ss->ms_ptr);
    order_moves(pos, sstack, ply, pvmove); ms = ss->ms_ptr;
    moves = 0;
    while(alpha < beta && !Threads[thread_id].stop &&
	  (move = pick_move(&ms, (ss+1)->ms_ptr, moves < 4))) {
      if(move_is_legal(pos, move) && see(pos, FROM(move), TO(move))==0) {
        moves++;
	ss->move = move;
        make_move(pos, move, u);
        value = -qsearch(pos, sstack, ply+1, -beta, -alpha, depth-PLY, 
			 (moves==1)? follow_pv : 0, thread_id);
        unmake_move(pos, move, u);
        if(value > bestvalue) {
          bestvalue = value;
          if(value > alpha) {
            alpha = value; update_pv(sstack, ply);
          }
        }
      }
    }
  }
 
  return alpha;
}


#if defined(SMP)

//// smp_update_pv() is used to update the PV at split points.  This is
//// slightly different from the normal update_pv() function, because we
//// also need to update the PV in the search stack of the parent node.
static void smp_update_pv(search_stack_t *psstack, search_stack_t *sstack,
			  int ply) {
  search_stack_t *ss = sstack+ply, *pss = psstack+ply;
  int i;
  ss->pv[ply] = pss->pv[ply] = ss->move;
  for(i=ply+1; (ss+1)->pv[i] != NOMOVE; i++) 
    ss->pv[i] = pss->pv[i] = (ss+1)->pv[i];
  ss->pv[i] = pss->pv[i] = NOMOVE;
}

//// smp_pick_move() replaces the normal pick_move() at split points.  It
//// uses a MUTEX lock to ensure that two threads do not pick the same move. 
static move_t smp_pick_move(split_point_t *sp) {
  move_t m;
  mutex_lock(sp->lock);
  m = pick_move(&(sp->current), sp->end, sp->moves < 16);
  mutex_unlock(sp->lock);
  return m;
}

//// smp_search() is used to start a search from a split point.  This function 
//// is called by each thread working at the split point.  It is similar to 
//// the normal search() function, but simpler.  Because we have already probed
//// the hash table, done a null move search, and searched the first move before
//// splitting, we don't have to repeat all this work in smp_search().  We also
//// don't need to store anything to the hash table here; this is take care of
//// after we return from, the split point.  
void smp_search(split_point_t *sp, int thread_id) {
  position_t *pos = sp->pos + thread_id;
  search_stack_t *sstack = sp->sstack[thread_id];
  int ply = sp->ply, depth = sp->depth, value, ext, side = pos->side, moves, i;
  search_stack_t *ss = sstack + ply;
  move_t move;
  undo_info_t u[1];
  move_stack_t *mstack = sp->mstack[thread_id];

  (ss+1)->ms_ptr = mstack;
  while(sp->alpha < sp->beta && !Threads[thread_id].stop &&
        (move = smp_pick_move(sp))) {
    if(move_is_legal(pos, move)) {
      mutex_lock(sp->lock);
      sp->moves++; moves = sp->moves;
      mutex_unlock(sp->lock);
      ss->move = move;
      make_move(pos, move, u);
      ext = extend(pos, ss, false);
      if(ext == 0 && moves >= 4 && 
	 ok_to_reduce(pos, sstack, sp->pvnode, ply, move)) {
	value = -search(pos, sstack, ply+1, -(sp->alpha + 1), -sp->alpha,
			depth - 2*PLY, 0, 1, thread_id);
      }
      else value = sp->alpha + 1;
      if(value > sp->alpha) {
	value = -search(pos, sstack, ply+1, -(sp->alpha + 1), -sp->alpha, 
			depth-PLY+ext, 0, 1, thread_id);
	if(value > sp->alpha && value < sp->beta) 
	  value = -search(pos, sstack, ply+1, -sp->beta, -sp->alpha, 
			  depth-PLY+ext, 0, 1, thread_id);
      }
      unmake_move(pos, move, u);

      if(Threads[thread_id].stop) break;
      mutex_lock(sp->lock);
      if(value > sp->bestvalue && !Threads[thread_id].stop) {
	sp->bestvalue = value;
	if(value > sp->alpha) {
	  sp->alpha = value; smp_update_pv(sp->parent_sstack, sstack, ply);
	  if(value >= sp->beta) {
	    inc_history(sp->parent_sstack, ply, move, side, depth, value);
            for(i = 0; i < Options->threads; i++)
              if(i != thread_id && (i == sp->master || sp->slaves[i]))
                Threads[i].stop = true;
          }
	}
      }
      mutex_unlock(sp->lock);
    }
  }
  mutex_lock(sp->lock);

  // If this is the master thread and we have been asked to stop because of
  // a beta cutoff higher up in the tree, stop all slave threads:
  if(sp->master == thread_id && Threads[thread_id].stop)
    for(i = 0; i < Options->threads; i++)
      if(sp->slaves[i]) Threads[i].stop = true;

  sp->cpus--;
  sp->slaves[thread_id] = 0;
  mutex_unlock(sp->lock);
}

#endif // defined(SMP)



uint64 perft(position_t &pos, int depth)
{
   move_stack_t mstack[256], *ms, *msend;
   move_t move;
   undo_info_t u[1];
   int n_msg;
   int n_moves, nmv, i, check;
   int cap = 0, ep = 0, prom = 0, castle = 0;
   int pmv, knmv, bmv, rmv, qmv, kmv, piece;
   uint64 nodes = 0;
   char currentfen[256], dofen[256], mvstr[16];

   if (depth == 0) return 1;

//#if defined(USE_CLUSTER_SEARCH)
//   n_msg = check_message();
//#endif

   ms = mstack;
   check = pos.check;
   if (check) {
     msend = generate_check_evasions(&pos, ms);
   } else {
     msend = generate_moves(&pos, ms);
   }
   n_moves = msend - mstack;
   //printf("%d\n", n_moves);
   ms = mstack;
   //while(move = pick_move(&ms, msend, false)) {

   //position_to_fen(&pos, currentfen);

   nmv = 0;
   cap = 0; ep = 0; prom = 0; castle = 0;
   pmv = 0; knmv = 0; bmv = 0; rmv = 0; qmv = 0; kmv = 0;
   for (i = 0; i < n_moves; i++) {
     move = mstack[i].move;
     if (move_is_legal(&pos, move)) {
       make_move(&pos, move, u);
       if (CAPTURE(move)) cap++;
       if (EP(move)) ep++;
       if (PROMOTION(move)) prom++;
       if (CASTLING(move)) castle++;
       piece = TypeOfPiece(PIECE(move));
       if (piece == PAWN) pmv++;
       if (piece == KNIGHT) knmv++;
       if (piece == BISHOP) bmv++;
       if (piece == ROOK) rmv++;
       if (piece == QUEEN) qmv++;
       if (piece == KING) kmv++;

       /*move2str(move, mvstr);
       position_to_fen(&pos, dofen);
       fprintf(genf, "%s %s %s\n", currentfen, mvstr, dofen);
       if (piece == PAWN) printf("%s %s %s\n", currentfen, mvstr, dofen);*/
       nmv++;
       nodes += perft(pos, depth - 1);
       unmake_move(&pos, move, u);
     }
   
     //if (try_split()) {
     //   break;
     //}

   }

   //position_to_fen(&pos, currentfen);
   //fprintf(genf, "%s %d %d %d %d %d   %d %d %d %d %d %d\n", currentfen, 
   //        nmv, cap, ep, castle, prom, pmv, knmv, bmv, rmv, qmv, kmv);

   return nodes;
}

void perft_test(char *fen, int depth)
{
  uint64 result = 0;
  position_t pos;
  int t1, t2;
  
  //genf = fopen("gen_move_num.txt", "w");
  //set_position(&pos, fen);
  t1 = get_time();
  result = perft(pos, depth);
  t2 = get_time();

  printf("======== Performance Testing depth = %d ========\n", depth);
  printf(" position: %s\n", fen);
  printf(" perft = %llu\n", result);
  printf(" time cost (ms) = %d\n", (t2 - t1));
  //printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
  printf("-------- Performance Testing depth = %d --------\n", depth);
}


void cluster_perft_test(const position_t &pos, int depth)
{
/*
  uint64 result = 0;
  int t1, t2;

  t1 = get_time();
  result = perft(pos, depth);
  t2 = get_time();

  printf("======== Performance Testing depth = %d ========\n", depth);
  //printf(" position: %s\n", fen);
  printf(" perft = %llu\n", result);
  printf(" time cost (ms) = %d\n", (t2 - t1));
  //printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
  printf("-------- Performance Testing depth = %d --------\n", depth);
*/
}
