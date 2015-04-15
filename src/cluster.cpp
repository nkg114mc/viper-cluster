//

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdio>

#include "viper.h"
#include "cluster.h"
#include "manager.h"

using namespace std;

#if defined(USE_CLUSTER_SEARCH)

#include <mpi.h>
#include <inttypes.h>

#include "cluster.h"


string MSG_NAME[20] = {
  "QUIT","INIT","RELAX","HELP","CANCEL","SPLIT","MERGE","STATUS","PING","PONG","ABORT",
  "DECLINE","OFFERHELP","ACCHELP", "SUBMIT_SPLIT", "WRITEBACK_SPLIT", "TRY_SPLIT", "SPLIT_OPPORTU"
};

string int2str(int n)
{
	stringstream ss("");
	ss << n;
	return ss.str();
}

int global_sp_id = 0;
int max_sp_stack_top = 0;

bool compare_position(position_t *p1, position_t *p2) {
/*
  uint8 board_[256];
  uint8 *board;
  list_t piece_list[256];
  move_t last_move;
  int ep_square;
  int castle_flags;
  int rule50;
  int gply;
  int side, xside;
  int piece_count[2][8];
  int material[2];
  int psq[2];
  int check, check_sqs[2];
  uint64 key, pkey, previous_keys[MAX_GAME_LENGTH];

 */
	int i, j;
	for (i = 0; i < 256; i++) {
		if (p1->board_[i] != p2->board_[i]) {
			return false;
		}
	}

	//if (p1->last_move != p2->last_move) {
	//	return false;
	//}

	if (p1->ep_square != p2->ep_square) {
		return false;
	}

	if (p1->castle_flags != p2->castle_flags) {
		return false;
	}

	if (p1->side != p2->side) {
		return false;
	}
	if (p1->xside != p2->xside) {
		return false;
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			if (p1->piece_count[i][j] != p2->piece_count[i][j]) {
				return false;
			}
		}
	}


	if (p1->check != p2->check) {
		return false;
	}

	for (i = 0; i < 2; i++) {
		if (p1->check_sqs[i] != p2->check_sqs[i]) {
			return false;
		}
	}

	for (i = 0; i < 256; i++) {
		if ((p1->piece_list[i].n != p2->piece_list[i].n) ||
			(p1->piece_list[i].p != p2->piece_list[i].p)) {
			for (i = 0; i < 256; i++) {
				cout << i << ": " << (int)(p2->piece_list[i].p)  <<" "<<  (int)(p2->piece_list[i].n) << "  ";
				cout << (int)(p1->piece_list[i].p) <<" "<< (int)(p1->piece_list[i].n)<< endl;
			}
			return false;
		}
	}

	return true;
}


void pop_next_task(split_point_t &sp, search_task_t &task)
{
	int i;
	move_t lmove;
	search_stack_t *ms;
	int n_move_left = 0;

	n_move_left = (sp.end - sp.current);
	
	if (n_move_left <= 0) {
		lmove = 0;
		task.sp_id = sp.sp_id;
		task.move_to_search = lmove;
		task.depth = sp.depth;
		task.src_host_id = sp.master;
		task.task_type = TASK_END;
		return;
	} else {
		do {
			lmove = ((sp.current)->move);
			sp.current++;
			if (move_is_legal(&(sp.parent_pos), lmove)) {
				task.sp_id = sp.sp_id;
				task.move_to_search = lmove;
				task.depth = sp.depth;
				task.src_host_id = sp.master;
				task.task_type = TASK_SEARCH;
				return;
			}
		} while (sp.current < sp.end);
	}

	cout << "3333333333333333333333333333333333333333" << endl;

	// no legal moves anymore
	lmove = 0;
	task.move_to_search = lmove;
	task.depth = sp.depth;
	task.task_type = TASK_END;
}

// return the number of remaining tasks in this split point 
int task_left(split_point_t &sp)
{
	int n_move_left = 0;
	n_move_left = (sp.end - sp.current);
	cout << "Number of move left: " << n_move_left << endl;
	return (n_move_left > 0);
}

////
//// host_t
////

// construction function
host_t::host_t(host_option_t &option)
{
	initialize(option.host_id, option.n_host, option.n_thread);
}

host_t::host_t()
{
}

host_t::~host_t()
{
}

void host_t::initialize(int hostId, int nHost, int nThread)
{
	int i;

	// set property
	host_id  = hostId;
	manager_id = nHost;
	n_host   = nHost;
	n_thread = nThread;

	// some consts
	status = HOST_IDLE;
	has_idle_host = 1;

	poll_peroid_nodes = 1000;
	since_last_nodes = 0;
	//host_is_runing = false;
	//host_should_stop = false;
	//host_work_waiting = false;
	sp_stack_top = 0; // split point stack
	//task_stack_top = 0; // task stack
	//my_master = -1;
	//working_sp_id = -1;

	total_searched_nodes = 0; // number of nodes statistic

	MinSplitDepth = 4; // default depth

	// status table
	//is_initialized = false;
	//status_table.my_host_id = host_id;
	global_sp_id = host_id * 1000000;

	// thread mutex lock
	mutex_init(&lock_mpi, NULL);

	// log file
	string log_fn = string("host-") + int2str(host_id) + string(".log");
	logf.open(log_fn.c_str());

}

void host_t::initialize(host_option_t &option)
{
	initialize(option.host_id, option.n_host, option.n_thread);
}

/*
void host_t::host_status_msg(status_message_t &msg) // msg is the return value
{
	//msg
	msg.host_id = host_id;
	msg.new_host_status = status;
}

void host_t::update_statustb(status_message_t &status_msg)
{
	//int hid = status_msg.host_id;
	//status_table.all_status[hid] = status_msg.new_host_status;
}
*/

//int host_t::offerhelp_respond(split_point_t &sp) {

//}


/*
// work
void host_t::host_idle_loop(split_point_t &sp)
{
	int n_message;
	split_point_t *sp_pointer;
	sp_pointer = &sp;
	uint32_t wait_cnt = 0;

	while (true) {


		//cout << "[" << host_id << "] " << "looooop!" << endl;
		n_message = check_message(sp);

		if (status == HOST_IDLE) {
			// no message
			//cout << "[" << host_id << "] " << "Wait Message ..." << endl;
			int exit_idle = 0;
			int hid = -1;
			int rnd_help_needer = -1, message_id;
			int last_helper = -1;


			// let's all helper gone since no work to do ====
			int i;
			//for (i = 0; i < runing_host_helpers.size(); i++) {
			//	ISend(runing_host_helpers[i], CANCEL);
			//}

			if (free_host_helpers.size() > 0) {
				for (i = 0; i < free_host_helpers.size(); i++) {
					ISend(free_host_helpers[i], CANCEL);
				}
				free_host_helpers.clear();
			}

			//runing_host_helpers.clear();
			// =============================================


			if (sp_pointer != NULL) { // I am a master

				///
				wait_cnt++;
				for (hid = 0; hid < n_host; hid++) {
					if (hid != host_id) {
						if (sp.slaves[hid] > 0) {
							cout << "(" << wait_cnt << ") Master [" << host_id << "] is still waiting for [" << hid << "]!" << endl;
						}
					}
					usleep(1000);
				}

			} else { // I am a slave

				if (my_master < 0) { // I have no master
					vector<int> other_hosts;
					for (hid = 0; hid < n_host; hid++) {
						if (hid != host_id) {  //&&
							//!contains_helper_host(hid)) {
							//if (contains_running_helper_host(hid)) {
							other_hosts.push_back(hid);
							//}
						}
					}

					int rnd_idx = rand() % other_hosts.size();
					rnd_help_needer = other_hosts[rnd_idx];

					// do you need help?
					cout << "[" << host_id << "] as slave can help!!!\n";
					Send(rnd_help_needer, OFFERHELP);
					cout << "[" << host_id << "]'s help got reply!!!\n";

					sleep_wait_for_message(rnd_help_needer);
					//wait_for_offerhelp_respond(sp, rnd_help_needer);
				}
			}

		} else if (status == HOST_IS_WORK_WAIT) {

			status = HOST_RUNNING;

			cout <<  "-[" << host_id << "] Providing help!\n";

			if (sp_pointer == NULL) { // master host do not need initialization
				logf << "Init === [" << host_id << "] " << "waiting for init msg before === " << endl;
				while (is_initialized == false) {
					//cout << "-[" << host_id << "] " << "waiting for init msg before " << endl;
					check_message(sp, my_master); // waiting for init msg
					//cout << "-[" << host_id << "] " << "waiting for init msg after" << endl;
				}
				logf << "Init === [" << host_id << "] " << "waiting for init msg after === " << endl;
			} else {

			}


			//status = HOST_RUNNING;

			// work now!
			cout << "-[" << host_id << "] " << "Begin to search!" << endl;
			if (status == HOST_RUNNING) { // if there is still work waiting...
				share_search(sp);
				status = HOST_IDLE;
			}
			cout << " [" << host_id << "] " << " finished searching!" << endl;




			cout << "-[" << host_id << "] searched nodes = " << total_searched_nodes << endl;


		} else if (status == HOST_QUIT) {
			cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;
			cout << "[" << host_id << "] split cnt = " << global_sp_id << endl;
			cout << "[" << host_id << "] exits!" << endl;
			//cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;
			//cout << "[" << host_id << "] " << "Time to exit!" << endl;
			return;
		}


		// master host should exit the loop if all the tasks in this split point
		// have been finished.
		if (sp_pointer != NULL && sp.cpus <= 0) {
			//int ssss = 99, l;
			//for (l = 0;l < ssss; l++) {
			//	cout << "cancel SPlit SPlit SPlit SPlit SPlit SPlit SPlit SPlit" << endl;
			//}
			break;
		}

	}
}
*/

void host_t::host_idle_loop(split_point_t &sp)
{
	int n_message;
	split_point_t *sp_pointer;
	sp_pointer = &sp;

	task_queue_t tsk_queu;
	char fen_string[256];
	bool host_should_stop = false;

	while (true) {

		n_message = check_message(sp, host_should_stop, tsk_queu, fen_string);

		if (host_should_stop) {
			cout << "[" << host_id << "] " << " should break idle loop" << endl;
			break;
		}
	}


	// write back information
	//if (sp_pointer != NULL) {
	//	sp.nodes =
	//}

	cout << "[" << host_id << "] " << "exit idle loop!" << endl;
}


// communication
void host_t::Send(int dest,int message) {
	logf << "Send to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	MPI_Send(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD);
}

void host_t::Send(int dest,int message,void* data,int size) {
	logf << "Send to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	MPI_Send(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD);
}

void host_t::ISend(int dest,int message) {
	logf << "ISend to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	//MPI_Isend(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, &mpi_request);
	MPI_Send(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD);
}

void host_t::ISend(int dest,int message,void* data,int size) {
	logf << "ISend to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	//MPI_Isend(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD, &mpi_request);
	MPI_Send(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD);
}

void host_t::Recv(int dest,int message) {
	MPI_Recv(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void host_t::Recv(int dest,int message,void* data,int size) {
	MPI_Recv(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

bool host_t::IProbe(int& source,int& message_id) {
	static MPI_Status mpi_status;
	int flag;
	MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,&flag,&mpi_status);
	if(flag) {
		message_id = mpi_status.MPI_TAG;
		source = mpi_status.MPI_SOURCE;
		return true;
	}
	return false;
}

void host_t::Non_Blocking_Send(int dest, int message) {
	mutex_lock(&lock_mpi);
	MPI_Isend(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, &mpi_request);
	mutex_unlock(&lock_mpi);
}

bool host_t::contains_helper_host(int hid) {
	int i;
	for (i = 0; i < runing_host_helpers.size(); i++) {
		if (runing_host_helpers[i] == hid) {
			return true;
		}
	}
	for (i = 0; i < free_host_helpers.size(); i++) {
		if (free_host_helpers[i] == hid) {
			return true;
		}
	}
	return false;
}

bool host_t::contains_free_helper_host(int hid) {
	int i;
	for (i = 0; i < free_host_helpers.size(); i++) {
		if (free_host_helpers[i] == hid) {
			return true;
		}
	}
	return false;
}

bool host_t::contains_running_helper_host(int hid) {
	int i;
	for (i = 0; i < runing_host_helpers.size(); i++) {
		if (runing_host_helpers[i] == hid) {
			return true;
		}
	}
	return false;
}

void host_t::add_helper_host(int hid) {
	cout << "Add helper [" << hid << "]!\n";
	free_host_helpers.push_back(hid);
}

void host_t::remove_helper_host(int hid) {
	int i;
	bool removed = false;
	//running hosts?
	for (i = 0; i < runing_host_helpers.size(); i++) {
		if (runing_host_helpers[i] == hid) {
			runing_host_helpers.erase(runing_host_helpers.begin() + i);
			removed = true;
			break;
		}
	}
	//  free hosts?
	for (i = 0; i < free_host_helpers.size(); i++) {
		if (free_host_helpers[i] == hid) {
			free_host_helpers.erase(free_host_helpers.begin() + i);
			removed = true;
			break;
			cerr << "Removed host should not be in the free host list!" << endl;
		}
	}

	assert(removed == true);
}

void host_t::remove_free_helper_host(int hid) {
	int i;
	bool removed = false;
	//  free hosts?
	for (i = 0; i < free_host_helpers.size(); i++) {
		if (free_host_helpers[i] == hid) {
			free_host_helpers.erase(free_host_helpers.begin() + i);
			removed = true;
			break;
			//cerr << "Removed host should not be in the free host list!" << endl;
		}
	}
	assert(removed == true);
}

void host_t::remove_running_helper_host(int hid) {
	int i;
	bool removed = false;
	//  free hosts?
	for (i = 0; i < runing_host_helpers.size(); i++) {
		if (runing_host_helpers[i] == hid) {
			runing_host_helpers.erase(runing_host_helpers.begin() + i);
			removed = true;
			break;
			//cerr << "Removed host should not be in the free host list!" << endl;
		}
	}
	assert(removed == true);
}



// return the number of message
int host_t::check_message(split_point_t &sp, bool &host_should_stop, task_queue_t &task_queue, char *fenstr) {
	return (check_message(sp, MPI_ANY_SOURCE, host_should_stop, task_queue, fenstr));
}

// return the number of message
int host_t::check_message(split_point_t &sp, int source, bool &host_stop, task_queue_t &task_queue, char *fenstr) // check messsge from particular source
{
	int flag;
	int n_message = 0;
	split_point_t *sp_pointer;
	sp_pointer = &sp;

	uint64_t temp_result = 0ULL; // the temporal result

	do {
		//Polling. MPI_Iprobe<->MPI_Recv is not thread safe.
		mutex_lock(&lock_mpi);
		//MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);
		MPI_Iprobe(source, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);


		// Message recieved?
		if (flag) {

			int message_id = mpi_status.MPI_TAG;
			int source = mpi_status.MPI_SOURCE;

			// write this to log file
			logf << "check Receive: " << get_time() << " " << MSG_NAME[message_id] << endl;

			if (message_id == SPLIT) {

				split_message_t split_msg;
				MPI_Recv(&split_msg, sizeof(split_message_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				split_msg.task.task_type = TASK_SEARCH;

				// set search task (push the task into task stack)
				//memcpy(&(task_stack[task_stack_top]), &(split_msg.task), sizeof(search_task_t));
				//task_stack_top++;
				task_queue.add_tail(split_msg.task);


				//status = HOST_IS_WORK_WAIT;
				mutex_unlock(&lock_mpi);

				cout << "get split msg:" << split_msg.task.move_to_search << " from " << source << endl;


				//// start working here ===================================================================
				share_search2(sp, task_queue, fenstr, split_msg.master_id, split_msg.stack_top);
				//// done working here ===================================================================


			} else if (message_id == INIT) {
				// task is comming! Get ready to work!
				init_message_t init_msg;
				MPI_Recv(&init_msg, sizeof(init_message_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);


				// init board
				//memcpy(&(parent_pos), &(init_msg.init_pos), sizeof(position_t));
				//parent_pos.board = parent_pos.board_ + 64;
				memcpy((fenstr), &(init_msg.fen), sizeof(char) * 256);


				cout << "init msg!" << endl;
				// ===========
				// change my status to "ready_to_work"
				status = HOST_RUNNING; //HOST_IS_WORK_WAIT;//HOST_REGISTER_WORK;
				//is_initialized = true;
				since_last_nodes = 0;
				//host_should_stop = false;
				task_queue.clear();
				// ===========


				// update my status, I am busy now
				update_message_t running_msg;
				running_msg.host_id = host_id;
				running_msg.new_host_status = HOST_RUNNING;
				ISend(manager_id, STATUS, (void*)(&running_msg), sizeof(update_message_t));


				// send a empty merge_msg, mainly for asking task from the master node!
				merge_message_t empty_merge_msg;
				empty_merge_msg.nodes = 0ULL; // empty nodes;
				empty_merge_msg.src_host_id = host_id;
				empty_merge_msg.master_id = init_msg.master_id;
				empty_merge_msg.stack_top = init_msg.stack_top;
				// send search result (Merge Msg)
				mutex_lock(&lock_mpi);
				MPI_Isend(&empty_merge_msg, sizeof(merge_message_t), MPI_BYTE, source, MERGE, MPI_COMM_WORLD, &mpi_request);
				mutex_unlock(&lock_mpi);


			} else if (message_id == WRITEBACK_SPLIT) {
				// task is comming! Get ready to work!
				writeback_msg_t write_back;
				MPI_Recv(&write_back, sizeof(writeback_msg_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				if (sp_pointer != NULL) {
					sp.nodes = write_back.nodes; // write back
				}

			} else if (message_id == CANCEL) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				cout << "get end-of-list msg from " << source << endl;

				// work done update my status to idle
				update_message_t running_msg;
				running_msg.host_id = host_id;
				running_msg.new_host_status = HOST_IDLE;
				ISend(manager_id, STATUS, (void*)(&running_msg), sizeof(update_message_t));
				

			} else if (message_id == SPLIT_OPPORTU) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				has_idle_host = 1;

			} else if (message_id == QUIT) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

				// stop working for current split point
				host_stop = true;


				///////////// show statistics ///////////////
				usleep(5000 * (host_id + 1));
				cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;
				cout << "[" << host_id << "] split cnt = " << global_sp_id << endl;
				cout << "[" << host_id << "] max_sp_stack_top = " << max_sp_stack_top << endl;
				/////////////////////////////////////////////

				mutex_unlock(&lock_mpi);

			} else {
				mutex_unlock(&lock_mpi);
			}

			// number of
			n_message++;

		} else {
			mutex_unlock(&lock_mpi);
		}

		// is it time to quit?
		if (host_stop) {
			break; // work done, exit
		}

	} while(flag);

	//cout << "Done message checking" << endl;

	return n_message;
}



// return the number of message
int host_t::wait_split_apply_response(int source) // check messsge from particular source
{
	int flag;
	int n_message = 0;

	while (1) {
		//Polling. MPI_Iprobe<->MPI_Recv is not thread safe.
		mutex_lock(&lock_mpi);
		//MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);
		MPI_Iprobe(source, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);


		// Message recieved?
		if (flag) {

			int message_id = mpi_status.MPI_TAG;
			int source = mpi_status.MPI_SOURCE;

			// write this to log file
			logf << "Receive: " << get_time() << " " << MSG_NAME[message_id] << endl;

			if (message_id == DECLINE) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				//return (DECLINE);
				return 0;

			} else if (message_id == ACCHELP) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				//return (ACCHELP);
				return 1;

			} else if (message_id == SPLIT_OPPORTU) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				has_idle_host = 1;


			} else {
				mutex_unlock(&lock_mpi);
			}

			// number of
			n_message++;

		} else {
			mutex_unlock(&lock_mpi);
		}


	}// while(flag);

	//return (DECLINE);;
	return -1;
}

void host_t::sleep_wait_for_message(int source)
{
	int msg_id, src_hid;
	if (status != HOST_QUIT) {
		//MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &mpi_status);
		MPI_Probe(source, MPI_ANY_TAG, MPI_COMM_WORLD, &mpi_status);
		//while (IProbe(src_hid, msg_id)) {
		//	usleep(1000);
		//	if (status == HOST_QUIT) return;
		//}
	}
}

int host_t::wait_for_offerhelp_respond(split_point_t &sp, int source)
{
}

bool host_t::idle_host_exist()
{/*
	int hid;
	int idle_host_count = 0;

	for (hid = 0; hid < n_host; hid++) {
		if (hid != host_id) {
			if (status_table.all_status[hid] != HOST_RUNNING) {
				idle_host_count++;
			}
		}
	}

	if (idle_host_count > 0) {
		return true;
	}
	return false;*/
	//if (free_host_helpers.size() > 0) {
	//	return true;
	//}
	//return false;

	if (has_idle_host  > 0) {
		return true;
	}
	return false;
}

bool host_t::host_is_avaliable(int hid)
{
	int i;
	// any free host exists?
	for (i = 0; i < free_host_helpers.size(); i++) {
		if (free_host_helpers[i] == hid) {
			return true;
		}
	}
	return false;
}


void host_t::share_search2(split_point_t &sp, task_queue_t &task_queue, char *fen, int master, int stack_top) {

	int n_msg;
	merge_message_t merge_msg;
	char fen2[256], mvstr[16];

	//
	move_t move_to_do;
	undo_info_t u[1];

	position_t pos;
	int depth, ply;
	int alpha, beta, bestval;
	uint64_t nodes = 0;
	uint64_t this_task_nodes = 0;
	search_task_t task;
	int src_host_id = -1;
	int task_type = -1;
	char move_name[16];


	//// start working here ===================================================================
	while (!task_queue.is_empty()) {

		// pop one task from task stack =====
		set_position(&pos, fen); // pos
		//int top_id = current_task_top; // task
		task_queue.pop_head(task);
		task_type = task.task_type;
		src_host_id = task.src_host_id;
		// ==================================

		char movename[16];
		cout << "[" << host_id << "] " << "pop out a move "<< string(move2str(task.move_to_search, movename)) << "!" << endl;


		if (task_type == TASK_SEARCH) {

			// information in task
			depth = task.depth;
			ply = task.ply;
			move_to_do = task.move_to_search;
			this_task_nodes = 0;

			cout << "[" << host_id << "] " << "get a task! From spid = " << task.sp_id << endl;
			cout << "[" << host_id << "] " << "Begin task content:" << endl;
			cout << "pos: " << fen << " depth " << (depth - 1) << endl;
			cout << "move_to_search = " << move2str(move_to_do, mvstr) << endl;
			cout << "[" << host_id << "] " << "End task content." << endl;

			// ===============================
			if (move_is_legal(&pos, move_to_do) && move_to_do != 0) {
				// do
				make_move(&pos, move_to_do, u);

				//position_to_fen(&pos, fen);
				logf << "Task {" << endl;
				logf << "  pos: " <<  fen << endl;
				logf << "  move: " << move2str(move_to_do, mvstr) << endl;
				logf << "  depth: " << (depth - 1) << endl;
				//logf << (int*)(pos.board_) << " " << (int*)(pos.board) << endl;
				logf << "}" << endl;

				this_task_nodes = cluster_perft(sp, pos, depth - 1);

				// undo
				unmake_move(&pos, move_to_do, u);
			} else {
				this_task_nodes = 0x0ULL;
				logf << "Task illegal {" << endl;
				logf << "  pos: " <<  fen << endl;
				logf << "  move: " << move2str(move_to_do, mvstr) << endl;
				logf << "}" << endl;
			}
			// ===============================

			cout << "[" << host_id << "] " << "finish a task! nodes = " << this_task_nodes << endl;
			cout << "[" << host_id << "] " << " sending merge!\n";
			logf << "spid(" << task.sp_id << ") " << move2str(move_to_do, mvstr) << " "<< this_task_nodes << endl;

			// construct merge_msg
			merge_msg.nodes = this_task_nodes;//nodes;
			merge_msg.src_host_id = host_id;
			merge_msg.master_id = master;
			merge_msg.stack_top = stack_top;

			// send search result (Merge Msg)
			mutex_lock(&lock_mpi);
			//MPI_Isend(&merge, MERGE_MESSAGE_SIZE(merge), MPI_BYTE, source, MERGE, MPI_COMM_WORLD, &mpi_request);
			MPI_Isend(&merge_msg, sizeof(merge_message_t), MPI_BYTE, manager_id, MERGE, MPI_COMM_WORLD, &mpi_request);
			mutex_unlock(&lock_mpi);

		}

	} // while task_queue is not end
}


void host_t::share_search(split_point_t &sp)
{
/*
	int n_msg;
	int master;
	merge_message_t merge_msg;
	split_point_t *sp_ptr;
	sp_ptr = &sp;
	char fen[256], mvstr[16];

	cout << "===> [" << host_id << "] sp_ptr = " << sp_ptr << endl;

	//
	move_t move_to_do;
	undo_info_t u[1];
	bool stop_current_task = false;

	position_t pos;	
	int depth, ply;
	int alpha, beta, bestval;
	uint64_t nodes = 0;
	uint64_t this_task_nodes = 0;
	search_task_t task;
	int src_host_id = -1;
	int task_type = -1;
	int current_task_top = 0;
	char move_name[16];
	task_queue_t *task_queue_ptr;


	cout << "[" << host_id << "] " << "Begin share search!" << endl;

	//if (sp_ptr != NULL) { // I am master host!
	host_should_stop = false;

	//}

	do {

		if (sp_ptr != NULL) { // I am master host!
			n_msg = check_message(sp);
		} else {
			n_msg = check_message(sp, my_master);
		}

		if (sp_ptr != NULL) { // I am master host!
			pop_next_task(sp, task);
			//task_queue_ptr = &(sp.task_queu);

			//if (task_left(sp)) {
			if (task.move_to_search != 0) { // a legal move
				// init pos
				copy_position(&parent_pos, &(sp.parent_pos)); // pos
				// get task from myself, rather than from message
				//sp.task_queu.add_tail(task); // push it into task stack
			} else {
				host_should_stop = true;
			}
		} else {
			task_queue_ptr = &(task_queue);
		}
		//stop_current_task = host_should_stop;		

		if (host_should_stop) {
			cout << "[" << host_id << "] " << " no move to search ,exit while!" << endl;
			break;
		}

		//while (!task_queue.is_empty()) {
		if (!task_queue_ptr->is_empty()) {
			// pop one task from task stack =====
			copy_position(&pos, &(parent_pos)); // pos
			init_piece_lists(&pos);
			//int top_id = current_task_top; // task
			task_queue_ptr->pop_head(task);//task = task_stack[top_id];
			task_type = task.task_type;
			src_host_id = task.src_host_id;
			//task_stack_top--; // pop!
			// ==================================

			char movename[16];
			cout << "[" << host_id << "] " << "pop out a move "<< string(move2str(task.move_to_search, movename)) << "!" << endl;


			if (task_type == TASK_SEARCH) {

				// information in task
				depth = task.depth;
				ply = task.ply;
				move_to_do = task.move_to_search;
				this_task_nodes = 0;


				cout << "[" << host_id << "] " << "get a task! From spid = " << task.sp_id << endl;
				cout << "[" << host_id << "] " << "Begin task content:" << endl;

				position_to_fen(&parent_pos, fen);
				set_position(&pos, fen);

				cout << "pos: " << fen << endl;
				cout << "move_to_search = " << move2str(move_to_do, mvstr) << endl;
				cout << "[" << host_id << "] " << "End task content." << endl;

				// ===============================
				if (move_is_legal(&pos, move_to_do) && move_to_do != 0) {
					// do
					make_move(&pos, move_to_do, u);


					position_to_fen(&pos, fen);
					logf << "Task {" << endl;
					logf << "  pos: " <<  fen << endl;
					logf << "  move: " << move2str(move_to_do, mvstr) << endl;
					//logf << (int*)(pos.board_) << " " << (int*)(pos.board) << endl;
					logf << "}" << endl;

					//search();
					this_task_nodes = cluster_perft(sp, pos, depth - 1);

					//nodes += this_task_nodes

					// undo
					unmake_move(&pos, move_to_do, u);
				} else {
					this_task_nodes = 0x0ULL;
					logf << "Task illegal {" << endl;
					logf << "  pos: " <<  fen << endl;
					logf << "  move: " << move2str(move_to_do, mvstr) << endl;
					logf << "}" << endl;
				}
				// ===============================

				cout << "[" << host_id << "] " << "finish a task! nodes = " << this_task_nodes << endl;

				if (sp_ptr != NULL) { // I am master host!

					sp.nodes += this_task_nodes;
					logf << "spid(" << task.sp_id << ") " << move2str(move_to_do, mvstr) << " " << this_task_nodes << endl;

				} else {

					cout << "[" << host_id << "] " << " sending merge!\n";
					logf << "spid(" << task.sp_id << ") " << move2str(move_to_do, mvstr) << " "<< this_task_nodes << endl;

					// construct merge_msg
					merge_msg.nodes = this_task_nodes;//nodes;
					merge_msg.src_host_id = host_id;

					// send search result (Merge Msg)
					//mutex_lock(&lock_mpi);
					//MPI_Isend(&merge, MERGE_MESSAGE_SIZE(merge), MPI_BYTE, source, MERGE, MPI_COMM_WORLD, &mpi_request);
					MPI_Isend(&merge_msg, sizeof(merge_message_t), MPI_BYTE, src_host_id, MERGE, MPI_COMM_WORLD, &mpi_request);
					//mutex_unlock(&lock_mpi);
				}

			} else if (task_type == TASK_END) {
				host_should_stop = true;
			}
		} // end of if tasks[] > 0

	} while (!host_should_stop); // stop current task?

	cout << "[" << host_id << "] " << "End share search!" << endl;

	if (sp_ptr != NULL) { // I am master host!
		// update n_cpu and slave for master hsot
		sp.cpus--;
		sp.slaves[host_id] = 0;
		cout << "Working CPU number: " << sp.cpus << endl;
	}
*/
}



/*
// split in perft
bool host_t::try_split(const position_t *p, int ply, int depth, uint64 &nodes,
	                   int *moves, move_stack_t *current, move_stack_t *end, int master)
{
	split_point_t *sp_pointer;
	int i;

	//mutex_lock(SMPLock); 

	// If the other thread is not idle or we have too many active split points,
	// don't split:
	if(!idle_host_exist() || sp_stack_top >= MaxActiveSplitPoints) {
		//mutex_unlock(SMPLock); 
		return false;
	}

	//if (global_sp_id > 0) {
	//	return false;
	//}

	cout << "Begin splitting ..." << endl;
	global_sp_id++;

	//sp_pointer = //SplitPointStack[master] + ActiveSplitPoints[master];
	//ActiveSplitPoints[master]++;
	sp_pointer = sp_stack + sp_stack_top;//SplitPointStack[master] + ActiveSplitPoints[master];
	sp_stack_top++;
	if (sp_stack_top > max_sp_stack_top) {
		max_sp_stack_top = sp_stack_top;
	}

	// Initialize the split point object:
	copy_position(&(sp_pointer->parent_pos), p);
	sp_pointer->sp_id = host_id * 1000000 + global_sp_id;
	sp_pointer->ply = ply; 
	sp_pointer->depth = depth;

	//split_point->alpha = *alpha; split_point->beta = *beta;
	//split_point->pvnode = pvnode;
	//split_point->bestvalue = *bestvalue;

	sp_pointer->master = master;
	sp_pointer->current = current; sp_pointer->end = end;
	sp_pointer->moves = *moves;
	sp_pointer->cpus = 0;
	sp_pointer->nodes = nodes;
	//split_point->parent_sstack = sstck;

	logf << "spid(" << sp_pointer->sp_id << ") init nodes " << nodes << endl;


	// clear all hosts status
	for (i = 0; i < n_host; i++) {
		sp_pointer->slaves[i] = 0;
	}

	// Make copies of the current position and search stack for each thread:
	for (i = 0; i < n_host; i++) {
		//if (thread_is_available(i, master) || i == master) {
		if (host_is_avaliable(i) || i == master) {
			if (i != master) {
				sp_pointer->slaves[i] = 1;
			}
			sp_pointer->cpus++;
		}
	}

	// Tell the threads that they have work to do.  This will make them leave
	// their idle loop.
	for (i = 0; i < n_host; i++) {
		//if(i == master || split_point->slaves[i]) {
		if (sp_pointer->slaves[i]) { // don't send message to master host
			//Threads[i].host_work_waiting = true;
			//Threads[i].idle = false;
			//Threads[i].stop = false;

			// record the free host id
			remove_free_helper_host(i);
			runing_host_helpers.push_back(i);

			// send init message
			init_message_t init_msg;
			copy_position(&(init_msg.init_pos), p);
			init_msg.master_id = master;
			init_msg.sp_id = (sp_pointer->sp_id);

			ISend(i, INIT, (void*)(&init_msg), sizeof(init_message_t));

		}
	}

	//mutex_unlock(SMPLock);

	// Everything is set up.  The master thread enters the idle loop, from
	// which it will instantly launch a search because its work_is_waiting
	// slot is 'true'.  We send the split point as the second parameter to
	// the idle loop, which means that the main thread will return from the
	// idle loop when all threads have finished their work at this split
	// point (i.e. when split_point->cpus == 0).

	logf << "Split { " << endl;
	logf << "  CPUs: " << sp_pointer->cpus << endl;
	logf << "  Master: " << sp_pointer->master << endl;
	logf << "  Stack_top: " << sp_stack_top << endl;
	logf << "  Split_depth: " << sp_pointer->depth << endl;
	logf << "  Slaves: ";
	for (i = 0; i < n_host; i++) {
		if (sp_pointer->slaves[i] > 0) {
			logf << i << " ";
		}
	} logf << endl;
	logf << "}" << endl;

	cout << "start main loop!" << endl;
	status = HOST_IS_WORK_WAIT;
	is_initialized = true;
	host_idle_loop(*sp_pointer);

	status = HOST_RUNNING; // return to singleton search

	// store it back to origin search function
	nodes = sp_pointer->nodes;
	//*alpha = split_point->alpha;
	//*beta = split_point->beta;
	//*bestvalue = split_point->bestvalue;
	//Threads[i].host_work_waiting = true;
	//Threads[i].idle = false;
	//Threads[i].stop = false;.stop = false;
	//Threads[master].idle = false;
	sp_stack_top--;

	//mutex_unlock(SMPLock);

	cout << "End splitting " << global_sp_id << " ..." << endl;
	return true;
}
*/

bool host_t::try_split(const position_t *p, int ply, int depth, uint64 &nodes,
	                   int *moves,  move_stack_t *mstack, move_stack_t *current, move_stack_t *end, int master)
{
	split_point_t *sp_pointer;
	char fen[256];
	int i, manager_response;

	//mutex_lock(SMPLock);

	// If the other thread is not idle or we have too many active split points,
	// don't split:
	if(sp_stack_top >= MaxActiveSplitPoints) {
		return false; // too many split points
	}


	// ok to have a split try
	has_idle_host = 0;

	// ask manager for idle host?
	//bool idle_host_exist = false;
	ISend(manager_id, TRY_SPLIT);
	manager_response = wait_split_apply_response(manager_id);
	//cout << manager_response << endl;
	if (!manager_response) {
		return false; // no idle hosts
	}


	cout << "Begin splitting ..." << endl;
	global_sp_id++;


	//sp_pointer = //SplitPointStack[master] + ActiveSplitPoints[master];
	//ActiveSplitPoints[master]++;
	sp_pointer = sp_stack + sp_stack_top;//SplitPointStack[master] + ActiveSplitPoints[master];
	sp_stack_top++;
	if (sp_stack_top > max_sp_stack_top) {
		max_sp_stack_top = sp_stack_top;
	}

	position_to_fen(p, fen); // get fen

	sp_msg_t split_point_msg;
	memcpy(split_point_msg.fen, fen, (sizeof(char)) * 256);
	split_point_msg.ply = ply;
	split_point_msg.depth = depth;

	//split_point->alpha = *alpha; split_point->beta = *beta;
	//split_point->pvnode = pvnode;
	//split_point->bestvalue = *bestvalue;

	split_point_msg.master_id = host_id;
	memcpy(split_point_msg.mstack, mstack, (sizeof(move_stack_t)) * 256);
	split_point_msg.current = current -  mstack;
	split_point_msg.end = end - mstack;
	split_point_msg.nodes = nodes;

	// sent split init
	ISend(manager_id, SUBMIT_SPLIT, (void*)(&split_point_msg), sizeof(sp_msg_t));


	cout << "start main loop!" << endl;
	host_idle_loop(*sp_pointer);

	status = HOST_RUNNING; // return to singleton search


	update_message_t running_msg;
	running_msg.host_id = host_id;
	running_msg.new_host_status = HOST_RUNNING;
	ISend(manager_id, STATUS, (void*)(&running_msg), sizeof(update_message_t));


	// store it back to origin search function 
	nodes = sp_pointer->nodes;
	sp_stack_top--;

	//mutex_unlock(SMPLock);

	cout << "End splitting " << global_sp_id << " ..." << endl;
	return true;
}


bool host_t::cluster_split(const position_t *p, search_stack_t *sstck, int ply, 
	   int *alpha, int *beta, bool pvnode, int *bestvalue, int depth, 
	   int *moves, move_stack_t *current, move_stack_t *end, int master)
{
	//for () {

	//}

	return false;

}

void host_t::main_host_work()
{
/*
	position_t main_pos;
	split_point_t some_sp;
	int depth;
	int t1, t2;
	
	char fenstr[256];
	char fen[256], turn[16], castle[16], epsq[16], n1[8], n2[8];
	uint64 result;

	FILE* logfp;
	logfp = fopen("result.txt", "w");


	while (1) {
	
		// input
		//scanf("%s%s%s%s%s%s", fen, turn, castle, epsq, n1, n2);
		char fenstr[256] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

		scanf("%d", &depth);
		if (depth < 0) {

			cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;
			cout << "[" << host_id << "] split cnt = " << global_sp_id << endl;
			cout << "[" << host_id << "] max_sp_stack_top = " << max_sp_stack_top << endl;

			for (int other = 0; other < (n_host + 1); other++) {
				if (other != host_id) {
					ISend(other, QUIT); // quit!
				}
			}

			break;
		}

		//sprintf(fenstr, "%s %s %s %s %s %s", fen, turn, castle, epsq, n1, n2);

		set_position(&main_pos, fenstr);

		cout << "Perft for the following position:" << endl;
		cout << fenstr << endl;

		// run parallel search
		t1 = get_time();
		status = HOST_RUNNING; // check status

		// Tell manager that I am working ============================================
		update_message_t running_msg;
		running_msg.host_id = host_id;
		running_msg.new_host_status = HOST_RUNNING;
		ISend(manager_id, STATUS, (void*)(&running_msg), sizeof(update_message_t));
		// ===========================================================================

		result = cluster_perft(some_sp, main_pos, depth);
		t2 = get_time();

		// Tell manager that I am done== ===========================================
		update_message_t idle_msg;
		idle_msg.host_id = host_id;
		idle_msg.new_host_status = HOST_IDLE;
		ISend(manager_id, STATUS, (void*)(&idle_msg), sizeof(update_message_t));
		// =========================================================================


		cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;

		printf("======== Performance Testing depth = %d ========\n", depth);
		printf(" position: %s\n", fen);
		printf(" perft = %llu\n", result);
		printf(" time cost (ms) = %d\n", (t2 - t1));
		//printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
		printf("-------- Performance Testing depth = %d --------\n", depth);

		////////////////////
		// output to file
		fprintf(logfp, "======== Performance Testing depth = %d ========\n", depth);
		fprintf(logfp, " position: %s\n", fen);
		fprintf(logfp, " perft = %llu\n", result);
		fprintf(logfp, " time cost (ms) = %d\n", (t2 - t1));
		//printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
		fprintf(logfp, "-------- Performance Testing depth = %d --------\n", depth);

	}

	fclose(logfp);
*/
	position_t main_pos;
	split_point_t some_sp;
	int depth;
	int t1, t2;
	
	//char fenstr[256];
	char fen[256], turn[16], castle[16], epsq[16], n1[8], n2[8];
	uint64 result;

	FILE* logfp;
	logfp = fopen("result.txt", "w");


	//while (1) {
	
		// input
		//scanf("%s%s%s%s%s%s", fen, turn, castle, epsq, n1, n2);
		char fenstr[256] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

		//scanf("%d", &depth);
		depth = 7;
		//sprintf(fenstr, "%s %s %s %s %s %s", fen, turn, castle, epsq, n1, n2);

		set_position(&main_pos, fenstr);

		cout << "Perft for the following position:" << endl;
		cout << fenstr << endl;

		// run parallel search
		t1 = get_time();
		status = HOST_RUNNING; // check status

		// Tell manager that I am working ============================================
		update_message_t running_msg;
		running_msg.host_id = host_id;
		running_msg.new_host_status = HOST_RUNNING;
		ISend(manager_id, STATUS, (void*)(&running_msg), sizeof(update_message_t));
		// ===========================================================================

		result = cluster_perft(some_sp, main_pos, depth);
		t2 = get_time();

		// Tell manager that I am done== ===========================================
		update_message_t idle_msg;
		idle_msg.host_id = host_id;
		idle_msg.new_host_status = HOST_IDLE;
		ISend(manager_id, STATUS, (void*)(&idle_msg), sizeof(update_message_t));
		// =========================================================================


		cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;

		printf("======== Performance Testing depth = %d ========\n", depth);
		printf(" position: %s\n", fen);
		printf(" perft = %llu\n", result);
		printf(" time cost (ms) = %d\n", (t2 - t1));
		//printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
		printf("-------- Performance Testing depth = %d --------\n", depth);

		////////////////////
		// output to file
		fprintf(logfp, "======== Performance Testing depth = %d ========\n", depth);
		fprintf(logfp, " position: %s\n", fen);
		fprintf(logfp, " perft = %llu\n", result);
		fprintf(logfp, " time cost (ms) = %d\n", (t2 - t1));
		//printf(" nodes/second = %d\n", (result / ((t2 - t1) / 1000)));
		fprintf(logfp, "-------- Performance Testing depth = %d --------\n", depth);


		// quit
		if (true) {

			cout << "[" << host_id << "] searched nodes = " << total_searched_nodes << endl;
			cout << "[" << host_id << "] split cnt = " << global_sp_id << endl;
			cout << "[" << host_id << "] max_sp_stack_top = " << max_sp_stack_top << endl;

			for (int other = 0; other < (n_host + 1); other++) {
				if (other != host_id) {
					ISend(other, QUIT); // quit!
				}
			}

			//break;
		}


	//}

	fclose(logfp);
}


int host_t::check_split_opportunity() {

	int flag;
	int n_message = 0;

	uint64_t temp_result = 0ULL; // the temporal result

	do {
		//Polling. MPI_Iprobe<->MPI_Recv is not thread safe.
		mutex_lock(&lock_mpi);
		MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);
		//MPI_Iprobe(source, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);


		// Message recieved?
		if (flag) {

			int message_id = mpi_status.MPI_TAG;
			int source = mpi_status.MPI_SOURCE;

			// write this to log file
			logf << "chck Receive: " << get_time() << " " << MSG_NAME[message_id] << endl;

			if (message_id == SPLIT_OPPORTU) {

				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				mutex_unlock(&lock_mpi);

				has_idle_host = 1;

			} else {
				mutex_unlock(&lock_mpi);

				break; // other messages
			}

			// number of
			n_message++;

		} else {
			mutex_unlock(&lock_mpi);
		}

	} while(flag);

	return n_message;
}


uint64 host_t::cluster_perft(split_point_t &sp, position_t &pos, int depth)
{
	move_stack_t mstack[256], *ms, *msend;
	move_t move;
	move_t legal_move[256];
	undo_info_t u[1];
	int n_msg, ply, n_legalmv;
	int n_moves, nmv, i, check, tried_moves, nleft;
	int cap = 0, ep = 0, prom = 0, castle = 0;
	int pmv, knmv, bmv, rmv, qmv, kmv, piece;
	uint64 nodes = 0;
	//char currentfen[256], dofen[256], mvstr[16];

	//cout << "depth = " << depth << endl;



	if (depth == 0) {
		//if (total_searched_nodes % 1000 == 0) {
		//	cout << "[" << host_id << "] depth = " << depth << " nodes = " << total_searched_nodes << endl;
		//}
		total_searched_nodes++;
		return (0x1ULL);
	}
	assert(depth > 0);

	//cout << "111111111111" << endl;
	// check input!
	since_last_nodes++;
	//if (since_last_nodes > poll_peroid_nodes) {
	//n_msg = check_message(sp);
	n_msg = check_split_opportunity();
	since_last_nodes = 0;
	// }
	//cout << "2222222222222" << endl;

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

	n_legalmv = 0;
	nmv = 0; tried_moves = 0;
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

			/*
			move2str(move, mvstr);
       	    position_to_fen(&pos, dofen);
       	    fprintf(genf, "%s %s %s\n", currentfen, mvstr, dofen);
       	    if (piece == PAWN) printf("%s %s %s\n", currentfen, mvstr, dofen);
       	    */
			nmv++;
			nodes += cluster_perft(sp, pos, depth - 1);
			unmake_move(&pos, move, u);
		}
		tried_moves++;

		nleft = n_moves - tried_moves;

		// try split
		if (n_host > 1 &&
			nleft >= 3 && //n_moves >= 3 &&
			depth >= 5 &&
			idle_host_exist()) {
			if (try_split(&pos, ply, depth, nodes, legal_move, mstack, (mstack + i + 1), msend, host_id)) {
				break;
			} else {
				//cout << "Split Fail ... " << endl;
				//cout << "stack_size = " << sp_stack_top << endl;
			}
		} else {
			//cout << "Split condition fail ... " << endl;
		}

	}

	//position_to_fen(&pos, currentfen);
	//fprintf(genf, "%s %d %d %d %d %d   %d %d %d %d %d %d\n", currentfen,
	//        nmv, cap, ep, castle, prom, pmv, knmv, bmv, rmv, qmv, kmv);

	return nodes;
}


///////////////////////////////

void task_queue_t::pop_head(search_task_t &task) {
	if (current_head < queue_tail) {
		memcpy(&(task), &(task_arr[current_head]), sizeof(search_task_t));
		current_head++;
	}
}
void task_queue_t::add_tail(search_task_t &task) {
	memcpy(&(task_arr[queue_tail]), &(task), sizeof(search_task_t));
	queue_tail++;
	cout << "tail_index = " << queue_tail << endl;
}

void task_queue_t::get_tail(search_task_t &task) {
	memcpy(&(task), &(task_arr[queue_tail - 1]), sizeof(search_task_t));
}

bool task_queue_t::is_empty(){
	if (current_head >= queue_tail) {
		return true;
	}
	return false;
}

void task_queue_t::clear()
{
	current_head = 0;
	queue_tail = 0;
}

/*
class task_queue_t {
public:
	search_task_t task_arr[256];
	int queue_tail, current_head;


};*/

#endif //#if defined(USE_CLUSTER_SEARCH)
