#ifndef MANAGER_H
#define MANAGER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "viper.h"
#include "pthread.h"
#include <mpi.h>
#include <inttypes.h>


#if defined(USE_CLUSTER_SEARCH)



struct sp_msg_t {
	int host_id;
	int master_id;
	int current, end;
	int ply;
	int depth;
	uint64_t nodes;
	char fen[256];
	move_stack_t mstack[256];
};

struct writeback_msg_t {
	int host_id;
	int master_id;
	uint64_t nodes;
};

class manager_t {
public:

	// some property
	int host_id;
	int n_host;
	int n_thread;

	// thread locks
	mutex_t lock_mpi;

	// status
	int status;
	bool host_should_stop;

	// about mpi
	MPI_Status  mpi_status;
	MPI_Request mpi_request;

	// some local data
	split_point_t sp_stack[8][8];
	int sp_stack_top[8];

	// about status
	int all_status[MAX_SPLIT_HOST];
	int all_master[MAX_SPLIT_HOST];

	// about search
	uint64_t total_searched_nodes;
	uint64_t since_last_nodes;
	uint64_t poll_peroid_nodes;
	int MinSplitDepth;

	// about log
	ofstream logf;
	int split_cnt[MAX_SPLIT_HOST];

	// --------------------
	manager_t();
	manager_t(host_option_t &option);
	~manager_t();
	void initialize(int hostId, int nHost, int nThread);
	void initialize(host_option_t &option);


	// communicate
/*
	int check_message(split_point_t &sp);
	int check_message(split_point_t &sp, int source);
	void sleep_wait_for_message(int source);
	//int wait_for_offerhelp_respond(split_point_t &sp, int source);
	//void host_status_msg(status_message_t&);
	//void update_statustb(status_message_t &status_msg);
*/
	void add_helper_host(int hid);
	void remove_helper_host(int hid);
	void remove_free_helper_host(int hid);
	void remove_running_helper_host(int hid);
	bool contains_helper_host(int hid);
	bool contains_free_helper_host(int hid);
	bool contains_running_helper_host(int hid);

	// send & receive
	void Non_Blocking_Send(int dest, int message);
	void Send(int dest,int message);
	void Send(int dest,int message,void* data,int size);
	void ISend(int dest,int message);
	void ISend(int dest,int message,void* data,int size);
	void Recv(int dest,int message);
	void Recv(int dest,int message,void* data,int size);
	bool IProbe(int& source, int& message_id);

	// about parallel search
	void manager_idle_loop();

	// split condition
	bool idle_host_exist(int master, int *avaliable_hosts);
	bool host_is_available(int slave, int master);

	// debugging
	void print_status_table(int *status);

};



#endif // if define(USE_CLUSTER_SEARCH)

#endif // #ifndef CLUSTER_H
