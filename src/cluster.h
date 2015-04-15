#ifndef CLUSTER_H
#define CLUSTER_H

//
#if defined(USE_CLUSTER_SEARCH)


#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <mpi.h>
#include <inttypes.h>

#include "viper.h"
#include "pthread.h"




using namespace std;
/*
class split_point_t {

public:
	board_t pos;

	// moves
	volatile int current, end;
	int n_moves;
	move_t all_moves[256];
	// depth
	int depth, ply;
	// value
	volatile int value;
	volatile int alpha, beta;
	
	// all member threads
	int slaves[MAX_SPLIT_THREAD];
	pthread_mutex_t split_lock[1];
	volatile int cpus;

	// some util functions
	//void pop_task();

};


class thread_t {
public:
	thread_t();
	~thread_t();	

	// local structures that thread search need
	board_t pos;

	// the local split point which need to be 
	split_point_t local_split_point;

	volatile uint64_t nodes;
	volatile bool stop;
	volatile bool running;
	volatile bool idle;
	volatile bool work_is_waiting;
	volatile bool print_currline;

	// thread and host id
	int thread_id;
	int host_id;

private:
};
*/

/*
struct status_table_t {
	int all_status[256];
	int my_host_id;
};
*/


// message structure
struct split_message_t {
	int message_id;
	//int depth;
	//split_point_t sp;
	search_task_t task;
	//int src_host_id;
	int master_id;
	int stack_top;
	//int 
};

struct merge_message_t {
	int message_id;

	int src_host_id;
	int master_id;
	int stack_top;


	// other values
	uint64_t nodes;

	// value
	int bestvalue;
	int alpha, beta;
	// pv
	int pv_len;
	move_t pv[128];
};

struct init_message_t {
	int message_id;
	//position_t init_pos;
	char fen[256];
	int stack_top;
	int master_id;
	int sp_id;
};

struct update_message_t {
	int message_id;
	int host_id;
	int new_host_status;
};

struct status_message_t {
	int message_id;
	int host_id;
	int new_host_status;
	int thread_status[16];
};

struct host_option_t {
	int host_id;
	int n_host;
	int n_thread;
	// host name
	char host_name;
	int namelen;
};

enum HOST_STATUS {
	HOST_IDLE, //HOST_REGISTER_WORK,
	HOST_IS_WORK_WAIT, HOST_RUNNING, HOST_QUIT
};

enum MESSAGE_TAG {
	QUIT = 0,INIT,RELAX,HELP,CANCEL,SPLIT,MERGE,STATUS,PING,PONG,ABORT,DECLINE,OFFERHELP, ACCHELP,
	SUBMIT_SPLIT, WRITEBACK_SPLIT, TRY_SPLIT, SPLIT_OPPORTU
};

enum TASK_TYPE {
	TASK_SEARCH, // regular search task
	TASK_END,    // end of move list, indicating not more search task for current split point
	TASK_UNKNOWN
};

extern string MSG_NAME[20];






class host_t {
public:

	// some property
	int host_id;
	int manager_id; // host manager id
	int n_host;
	int n_thread;

	// thread mutex lock for send and recieve message
	//pthread_mutex_t lock_mpi;
	mutex_t lock_mpi;

	// status
	int status;
	int has_idle_host; // which means we can try split
	//bool is_initialized;
	//status_table_t status_table;// other hosts' status
	vector<int> free_host_helpers;
	vector<int> runing_host_helpers;

	// about mpi
	MPI_Status  mpi_status;
	MPI_Request mpi_request;

	// some local data
	split_point_t sp_stack[MAX_SPLIT_HOST];
	int sp_stack_top;

	// about search
	uint64_t total_searched_nodes;
	uint64_t since_last_nodes;
	uint64_t poll_peroid_nodes;
	int MinSplitDepth;

	// about log
	ofstream logf;
	int split_cnt;

	// --------------------
	host_t();
	host_t(host_option_t &option);
	~host_t();
	void initialize(int hostId, int nHost, int nThread);
	void initialize(host_option_t &option);


	// communicate
	void Non_Blocking_Send(int dest, int message);
	int check_message(split_point_t &sp, bool &host_should_stop, task_queue_t &task_queue, char *fenstr);
	int check_message(split_point_t &sp, int source, bool &host_should_stop, task_queue_t &task_queue, char *fenstr);
	int check_split_opportunity();
	int wait_split_apply_response(int source);
	void sleep_wait_for_message(int source);
	int wait_for_offerhelp_respond(split_point_t &sp, int source);
	//void host_status_msg(status_message_t&);
	//void update_statustb(status_message_t &status_msg);

	void add_helper_host(int hid);
	void remove_helper_host(int hid);
	void remove_free_helper_host(int hid);
	void remove_running_helper_host(int hid);
	bool contains_helper_host(int hid);
	bool contains_free_helper_host(int hid);
	bool contains_running_helper_host(int hid);

	// send & receive
	void Send(int dest,int message);
	void Send(int dest,int message,void* data,int size);
	void ISend(int dest,int message);
	void ISend(int dest,int message,void* data,int size);
	void Recv(int dest,int message);
	void Recv(int dest,int message,void* data,int size);
	bool IProbe(int& source, int& message_id);

	// main host
	void main_host_work();

	// about parallel search
	void host_idle_loop(split_point_t& sp);
	bool idle_host_exist();
	bool host_is_avaliable(int hid);
	bool try_split(const position_t *p, int ply, int depth, uint64 &nodes,
	               int *moves, move_stack_t *mstack, move_stack_t *current, move_stack_t *end, int master);
	bool cluster_split(const position_t *p, search_stack_t *sstck, int ply, 
	   int *alpha, int *beta, bool pvnode, int *bestvalue, int depth, 
	   int *moves, move_stack_t *current, move_stack_t *end, int master);
	void share_search(split_point_t &sp);
	void share_search2(split_point_t &sp, task_queue_t &task_queue, char *fen, int master, int stack_top);
	uint64 cluster_perft(split_point_t &sp, position_t &pos, int depth);


	// storage host

/*
	thread_t Threads[MaxNumOfThreads];


	static bool AllThreadsShouldExit = false;

	mutex_t SMPLock[1], IOLock[1], WaitLock[1];

	pthread_cond_t WaitCond[1];


	static int ActiveSplitPoints[MaxNumOfThreads];
	static const int MaxActiveSplitPoints = 8;
	static split_point_t SplitPointStack[MaxNumOfThreads][MaxActiveSplitPoints];
*/

};

extern int global_sp_id;
extern int max_sp_stack_top;

extern void pop_next_task(split_point_t &sp, search_task_t &task);
extern string int2str(int n);



//extern host_t local_host;

#endif // if define(USE_CLUSTER_SEARCH)

#endif // #ifndef CLUSTER_H
