#include "viper.h"

thread_t Threads[MaxNumOfThreads];

#if defined(SMP)

static bool AllThreadsShouldExit = false;

mutex_t SMPLock[1], IOLock[1], WaitLock[1];

#ifndef _MSC_VER
pthread_cond_t WaitCond[1];
#else
HANDLE SitIdleEvent = INVALID_HANDLE_VALUE;
#endif

static int ActiveSplitPoints[MaxNumOfThreads];

static split_point_t SplitPointStack[MaxNumOfThreads][MaxActiveSplitPoints];

void idle_loop(int thread_id, split_point_t *wait_sp) {
  Threads[thread_id].running = true;
  while(1) {
    if(AllThreadsShouldExit && thread_id != 0) break;

    // If we are not thinking, wait for a condition to be signaled instead
    // of wasting CPU time polling for work:
    while(thread_id != 0 && 
          (RSI->thinking_status == IDLE || thread_id >= Options->threads)) {
#ifndef _MSC_VER
      mutex_lock(WaitLock);
      if(RSI->thinking_status == IDLE || thread_id >= Options->threads) 
        pthread_cond_wait(WaitCond, WaitLock);
      mutex_unlock(WaitLock);
#else
      WaitForSingleObject(SitIdleEvent, INFINITE);
#endif
    }

    // If this thread has been assigned work, launch a search:
    if(Threads[thread_id].work_is_waiting) {
      Threads[thread_id].work_is_waiting = false;
      smp_search(Threads[thread_id].split_point, thread_id);
      Threads[thread_id].idle = true;
    }

    // If this thread is the master of a split point and all threads
    // have finished their work at this split point, return from the
    // idle loop:
    if(wait_sp != NULL && wait_sp->cpus == 0) return;
  }
  Threads[thread_id].running = false;
}

#ifndef _MSC_VER
static void *init_thread(void *thread_id) {
  idle_loop(*(int *)thread_id, NULL); return NULL;
}
#else
static DWORD WINAPI win_init_thread(LPVOID n) {
  idle_loop(1, NULL); return 0;
}
#endif

void init_split_point_stack(void) {
  int i, j;
  for(i = 0; i < MaxNumOfThreads; i++)
    for(j = 0; j < MaxActiveSplitPoints; j++) 
      mutex_init(SplitPointStack[i][j].lock, NULL);
}

void destroy_split_point_stack(void) {
  int i, j; 
  for(i = 0; i < MaxNumOfThreads; i++)
    for(j = 0; j < MaxActiveSplitPoints; j++) 
      mutex_destroy(SplitPointStack[i][j].lock);
}

void init_threads(int n) {
  volatile int i;

  pthread_t pthread[1];
  
  init_pawn_hash_table(n);
  for(i = 0; i < MaxNumOfThreads; i++) ActiveSplitPoints[i] = 0;

  // Initialize global locks and condition objects:
  mutex_init(SMPLock, NULL); 
  mutex_init(IOLock, NULL);
  mutex_init(WaitLock, NULL);
#ifndef _MSC_VER
  pthread_cond_init(WaitCond, NULL);
#else
  SitIdleEvent = CreateEvent(0, FALSE, FALSE, 0);
#endif

  // All threads except the main thread should be initialized to idle state:
  for(i = 1; i < n; i++) {
    Threads[i].stop = false;
    Threads[i].work_is_waiting = false;
    Threads[i].idle = true;
  }

  // Launch the helper threads (only one thread at the moment, support for
  // more than 2 CPUs will be added later).
  for(i = 1; i < n; i++) {
#ifndef _MSC_VER
    pthread_create(pthread, NULL, init_thread, (void *)(&i));
#else
    {
      DWORD iID[1];
      CreateThread(NULL, 0, win_init_thread, "Thread 1", 0, iID);
    }
#endif
    // Wait until the thread has finished launching:
    while(!Threads[i].running);
  }
}

static bool thread_is_available(int slave, int master) {
  if(!Threads[slave].idle) return false;
  if(ActiveSplitPoints[slave] == 0) 
    // No active split points means that the thread is available as a slave
    // for any other thread.
    return true;
  if(Options->threads == 2) 
    return true;
  // Apply the "helpful master" concept if possible.
  if(SplitPointStack[slave][ActiveSplitPoints[slave]-1].slaves[master])
    return true;
  return false;
}
  
bool idle_thread_exists(int master) {
  int i;
  for(i = 0; i < Options->threads; i++) 
    if(i != master && thread_is_available(i, master)) return true;
  return false;
}

bool split(const position_t *p, search_stack_t *sstck, int ply, 
	   int *alpha, int *beta, bool pvnode, int *bestvalue, int depth, 
	   int *moves, move_stack_t *current, move_stack_t *end, int master) {
  split_point_t *split_point;
  int i;

  mutex_lock(SMPLock); 

  // If the other thread is not idle or we have too many active split points,
  // don't split:
  if(!idle_thread_exists(master) || 
     ActiveSplitPoints[master] >= MaxActiveSplitPoints) {
    mutex_unlock(SMPLock); 
    return false;
  }

  split_point = SplitPointStack[master] + ActiveSplitPoints[master];
  ActiveSplitPoints[master]++;

  // Initialize the split point object:
  split_point->ply = ply; split_point->depth = depth;
  split_point->alpha = *alpha; split_point->beta = *beta;
  split_point->pvnode = pvnode;
  split_point->bestvalue = *bestvalue;
  split_point->master = master;
  split_point->current = current; split_point->end = end;
  split_point->moves = *moves;
  split_point->cpus = 0;
  split_point->parent_sstack = sstck;
  for(i = 0; i < Options->threads; i++) split_point->slaves[i] = 0;

  // Make copies of the current position and search stack for each thread:
  for(i = 0; i < Options->threads; i++) 
    if(thread_is_available(i, master) || i == master) {
      copy_position(split_point->pos + i, p);
      memcpy(split_point->sstack[i], sstck, (ply+1)*sizeof(search_stack_t));
      Threads[i].split_point = split_point;
      if(i != master) split_point->slaves[i] = 1;
      split_point->cpus++;
    }

  // Tell the threads that they have work to do.  This will make them leave
  // their idle loop.
  for(i = 0; i < Options->threads; i++) 
    if(i == master || split_point->slaves[i]) {
      Threads[i].work_is_waiting = true;
      Threads[i].idle = false;
      Threads[i].stop = false;
    }

  mutex_unlock(SMPLock);

  // Everything is set up.  The master thread enters the idle loop, from 
  // which it will instantly launch a search because its work_is_waiting 
  // slot is 'true'.  We send the split point as the second parameter to 
  // the idle loop, which means that the main thread will return from the 
  // idle loop when all threads have finished their work at this split 
  // point (i.e. when split_point->cpus == 0).
  idle_loop(master, split_point);

  // We have returned from the idle loop, which means that all threads are
  // finished.  Update alpha, beta and bestvalue and return:
  mutex_lock(SMPLock);
  *alpha = split_point->alpha; *beta = split_point->beta; 
  *bestvalue = split_point->bestvalue;

  Threads[master].stop = false; Threads[master].idle = false;
  ActiveSplitPoints[master]--;

  mutex_unlock(SMPLock);
  return true;
}

static bool some_thread_is_running(void) {
  int i;
  for(i = 1; i < MaxNumOfThreads; i++)
    if(Threads[i].running) return true;
  return false;
}

void stop_threads(void) {
  int i;
  if(MaxNumOfThreads > 1) {
    RSI->thinking_status = THINKING; // HACK
    Options->threads = MaxNumOfThreads; // HACK
#ifndef _MSC_VER
    pthread_mutex_lock(WaitLock);
    pthread_cond_broadcast(WaitCond);
    pthread_mutex_unlock(WaitLock);
#else
    SetEvent(SitIdleEvent);
#endif
    for(i = 0; i < MaxNumOfThreads; i++) Threads[i].stop = true;
    AllThreadsShouldExit = true;
    while(some_thread_is_running());
  }
}

#endif // defined(SMP)

