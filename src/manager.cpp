#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <inttypes.h>
#include <pthread.h>

#include "viper.h"
#include "cluster.h"
#include "manager.h"



////
//// manager_t
////

int max_stck[MAX_SPLIT_HOST];

// construction function
manager_t::manager_t(host_option_t &option)
{
	initialize(option.host_id, option.n_host, option.n_thread);
}

manager_t::manager_t()
{
}

manager_t::~manager_t()
{
}

void manager_t::initialize(host_option_t &option) {
	initialize(option.host_id, option.n_host, option.n_thread);
}

void manager_t::initialize(int hostId, int nHost, int nThread)
{
	int i;

	// set property
	host_id  = hostId;
	n_host   = nHost;
	n_thread = nThread;

	// some consts
	status = HOST_IDLE;
	poll_peroid_nodes = 1000;
	since_last_nodes = 0;
	host_should_stop = false;

	for (i = 0; i < n_host; i++) {
		all_status[i] = HOST_IDLE; // idle status
		all_master[i] = -1; // current master, -1 indicates no master
	}

	// set split_top as 0
	for (i = 0; i < n_host; i++) {
		sp_stack_top[i] = 0; // split point stack
		split_cnt[i] = 0;
		max_stck[i] = 0;
	}

	total_searched_nodes = 0; // number of nodes statistic
	MinSplitDepth = 4; // default depth

	// thread mutex lock
	mutex_init(&lock_mpi, NULL);

	// log file
	string log_fn = string("host-") + int2str(host_id) + string(".log");
	logf.open(log_fn.c_str());

}


void manager_t::manager_idle_loop() {

	int flag, i;
	int n_message = 0;
	split_point_t *sp_pointer;

	host_should_stop = false;

	while (!host_should_stop) {

		do {
			//Polling. MPI_Iprobe<->MPI_Recv is not thread safe.
			mutex_lock(&lock_mpi);
			MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &mpi_status);


			// Message recieved?
			if (flag) {

				int message_id = mpi_status.MPI_TAG;
				int source = mpi_status.MPI_SOURCE;

				// write this to log file
				logf << "Receive: " << get_time() << " " << MSG_NAME[message_id] << endl;

				if (message_id == SUBMIT_SPLIT) {

					sp_msg_t sp_msg;
					MPI_Recv(&sp_msg, sizeof(sp_msg_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					mutex_unlock(&lock_mpi);

					// set up split point
					if (true) {
						int master = source;
						int stck_top =  sp_stack_top[source];
						sp_pointer = &(sp_stack[source][stck_top]);//SplitPointStack[master] + ActiveSplitPoints[master];
						sp_stack_top[source]++;
						if (sp_stack_top[source] > max_stck[source]) {
							max_stck[source] = sp_stack_top[source];
						}

						// Initialize the split point object:
						//copy_position(&(sp_pointer->parent_pos), p);
						memcpy(sp_pointer->fen, sp_msg.fen, (sizeof(char)) * 256);
						set_position(&(sp_pointer->parent_pos), sp_msg.fen);

						//sp_pointer
						sp_pointer->sp_id = host_id * 1000000 + global_sp_id;
						sp_pointer->ply = sp_msg.ply;
						sp_pointer->depth = sp_msg.depth;

						//split_point->alpha = *alpha; split_point->beta = *beta;
						//split_point->pvnode = pvnode;
						//split_point->bestvalue = *bestvalue;

						sp_pointer->master = sp_msg.master_id;//source;
						memcpy(sp_pointer->mstack2, sp_msg.mstack, (sizeof(move_stack_t)) * 256);
						sp_pointer->current = sp_pointer->mstack2 + sp_msg.current; sp_pointer->end = sp_pointer->mstack2 + sp_msg.end;
						sp_pointer->cpus = 0;
						sp_pointer->nodes = sp_msg.nodes;

						logf << "spid(" << sp_pointer->sp_id << ") init nodes " << sp_msg.nodes << endl;


						// clear all hosts status
						for (i = 0; i < n_host; i++) {
							sp_pointer->slaves[i] = 0;
						}

						// Make copies of the current position and search stack for each thread:
						for (i = 0; i < n_host; i++) {
							//if (host_is_avaliable(i) || i == master) {
							logf << "submit_split at [" << i << "]: " << all_status[i] << " " << all_master[i] << endl;
							if ((i == master) ||
								(all_status[i] == HOST_IDLE  && all_master[i] == master)) {

								logf << "set slave " << i << endl;
								sp_pointer->slaves[i] = 1;
								sp_pointer->cpus++;
							}
						}

						// Tell the threads that they have work to do.  This will make them leave
						// their idle loop.
						for (i = 0; i < n_host; i++) {
							//if(i == master || split_point->slaves[i]) {
							if (sp_pointer->slaves[i]) { // don't send message to master host
								// send init message
								init_message_t init_msg;
								//copy_position(&(init_msg.init_pos), p);
								memcpy(init_msg.fen, sp_pointer->fen, (sizeof(char)) * 256);
								init_msg.master_id = master;
								init_msg.stack_top = stck_top;
								init_msg.sp_id = (sp_pointer->sp_id);

								// set the slave host in "RUNNING" status
								all_status[i] = HOST_RUNNING; // set it running before it is really running

								ISend(i, INIT, (void*)(&init_msg), sizeof(init_message_t));
							}
						}

						// =========== log =================
						logf << "Split { " << endl;
						logf << "  CPUs: " << sp_pointer->cpus << endl;
						logf << "  Master: " << sp_pointer->master << endl;
						logf << "  Stack_top: " << sp_stack_top << endl;
						logf << "  Split_depth: " << sp_pointer->depth << endl;
						logf << "  Slaves: ";
						for (i = 0; i < n_host; i++) {
							if (sp_pointer->slaves[i]) {
								logf << i << " ";
							}
						} logf << endl;
						logf << "}" << endl;
						// =================================
					}
				} else if (message_id == TRY_SPLIT) {

					MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
					mutex_unlock(&lock_mpi);

					// is this split ok?
					//ISend(source, DECLINE);
					int avaliable_helpers[64];

					if (sp_stack_top[source] < 4 &&
						idle_host_exist(source, avaliable_helpers)) {

						// make a reservation so that other "TRY_SPLIT"
						int this_master = source; // host that apply for a split
						for (i = 0; i < n_host; i++) {
							if ((avaliable_helpers[i] > 0) || (i == this_master)) {
								all_master[i] = this_master;
							}
						}

						// ==========================
						logf << "[" << host_id << "] approve split application with ";
						for (i = 0; i < n_host; i++) {
							if (all_master[i] == this_master) {
								logf << i << " ";
							}
						} logf << endl;
						// =========================

						ISend(source, ACCHELP);

					} else {
						logf << "[" << host_id << "] split application got rejected!" << endl;
						ISend(source, DECLINE);
					}


				} else if (message_id == MERGE) {

					// merge
					merge_message_t merge_msg;
					MPI_Recv(&merge_msg, sizeof(merge_message_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

					uint64_t nodes = merge_msg.nodes;
					int master = merge_msg.master_id;
					int stack_top = merge_msg.stack_top;

					cout << " get merge msg from " << source << " for split stack " << stack_top << " and master " << master << " with value " << nodes << endl;



					sp_pointer = &(sp_stack[master][stack_top]);

					// record the number of nodes!
					sp_pointer->nodes += nodes;


					// next task at split point?
					search_task_t next_task;
					pop_next_task(*sp_pointer, next_task);
					if (next_task.move_to_search != 0) { // a legal move

						split_message_t next_split_msg;
						next_split_msg.task = next_task; // send next task
						next_split_msg.master_id = master; // from my host_id
						next_split_msg.stack_top = stack_top;

						cout << "send split move " << next_task.move_to_search << endl;

						// sent new task
						ISend(source, SPLIT, (void*)(&next_split_msg), sizeof(split_message_t));

					} else {
						// sent cancel to let this host be in idle

						////
						/*
						sp_pointer->cpus--;
						sp_pointer->slaves[source] = 0;
						if (source == (sp_pointer->master)) {

							if (sp_pointer->cpus == 0) {

								sp_stack_top[master]--;

								// write back the result!
								writeback_msg_t write_back;
								write_back.master_id = master;
								write_back.host_id = source;
								write_back.nodes = sp_pointer->nodes;
								ISend(source, WRITEBACK_SPLIT, (void*)(&write_back), sizeof(writeback_msg_t));
								usleep(1000);

								// let master quit
								ISend(source, QUIT);
							} else {
								ISend(source, CANCEL); // idle master
							}

						} else {
							ISend(source, CANCEL);
						}
						 */


						sp_pointer->cpus--;
						sp_pointer->slaves[source] = 0;
						if (sp_pointer->cpus == 0) {

							sp_stack_top[master]--;

							// write back the result!
							writeback_msg_t write_back;
							write_back.master_id = master;
							write_back.host_id = source;
							write_back.nodes = sp_pointer->nodes;
							//ISend(source, WRITEBACK_SPLIT, (void*)(&write_back), sizeof(writeback_msg_t));
							ISend(master, WRITEBACK_SPLIT, (void*)(&write_back), sizeof(writeback_msg_t));
							usleep(1000);

							// let master quit
							if (source == master) {
								ISend(source, QUIT); // ISend(master, QUIT);
							} else {
								ISend(source, CANCEL);
								usleep(1000);
								ISend(master, QUIT);
							}


						} else { // not done yet, other cpus is still working

							if (source == master) {
								// ISend(source, CANCEL); // idle master, might be used as helpful master
							} else {
								ISend(source, CANCEL); // slave is free, and is avaliable to help others
							}
						}
					}

					mutex_unlock(&lock_mpi);


				} else if (message_id == STATUS) {

					update_message_t update_msg;
					MPI_Recv(&update_msg, sizeof(update_message_t), MPI_BYTE, source, message_id, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

					all_status[source] = update_msg.new_host_status; //
					if (all_status[source] == HOST_IDLE) {
						all_master[source] = -1; // I have no master now

						// tell others that there are some idle host, and a split might be allowed
						for (i = 0; i < n_host; i++) {
							if ((i != source) &&
								(all_status[i] == HOST_RUNNING)) {
								ISend(i, SPLIT_OPPORTU);
							}
						}

					} else if (all_status[source] == HOST_RUNNING) {
						//if (all_master[source] == host_id) {
						//	assert(all_master[source] >= 0); //
						//}
					}

					print_status_table(all_status);

					mutex_unlock(&lock_mpi);

				} else if (message_id == QUIT) {

					mutex_unlock(&lock_mpi);

					usleep(5000 * host_id);
					for (i = 0; i < n_host; i++ ) {
						cout << "["<< host_id << "] max_sp_stack_top = " << max_stck[i] << endl;
					}

					host_should_stop = true;
					break;

				} else {
					mutex_unlock(&lock_mpi);
				}

				// number of
				n_message++;

			} else {
				mutex_unlock(&lock_mpi);

			}



		} while(flag);

		if (host_should_stop) {
			break;
		}


	}

	cout << "Manager host exit!" << endl;
};


void manager_t::print_status_table(int *status) {

	int i;
	string status_name[5] = { "HOST_IDLE", "HOST_IS_WORK_WAIT", "HOST_RUNNING", "HOST_QUIT" };
	cout << "{ ";
	for (i = 0; i < n_host; i++) {
		if (i > 0) {
			cout << ", ";
		}
		cout << status_name[status[i]];
	}
	cout << " }" << endl;
	//===========================
	logf << "{ ";
	for (i = 0; i < n_host; i++) {
		if (i > 0) {
			logf << ", ";
		}
		logf << status_name[status[i]];
	}
	logf << " }" << endl;

}

bool manager_t::host_is_available(int slave, int master) {
	assert(slave != master);
/*
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
*/
	if ((all_status[slave] != HOST_IDLE) || // not idle
		all_master[slave] >= 0) { // has been reserved
		return false;
	}

	// slave has no split yet
	if (sp_stack_top[slave] == 0) {
	    // No active split points means that the host is available as a slave
	    // for any other thread.
	    return true;
	}

	// always ok when there is only two hosts
	if (n_host == 2) {
		return true;
	}
/*
	// "helpful master"!
	int active_slave_sp = sp_stack_top[slave];
	if (sp_stack[slave][active_slave_sp - 1].slaves[master]) {
		 return true;
	}
*/
	// not available
	return false;
}

bool manager_t::idle_host_exist(int master, int *avaliable_hosts)
{
	int hid;
	int idle_host_count = 0;

	for (hid = 0; hid < n_host; hid++) {
		avaliable_hosts[hid] = 0;
	}

	for (hid = 0; hid < n_host; hid++) {
		if (hid != master) {
			/*
			if (all_status[hid] == HOST_IDLE) { // host is in idle
				if (all_master[hid] < 0) { // and was not reserved
					idle_host_count++;
					avaliable_hosts[hid] = 1;
				}
			}
			*/
			if (host_is_available(hid, master)) {
				idle_host_count++;
				avaliable_hosts[hid] = 1;
			}
		}
	}

	if (idle_host_count > 0) { // some host is in idle
		return true;
	}
	return false;
}


// communication
void manager_t::Send(int dest,int message) {
	logf << "Send to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	MPI_Send(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD);
}

void manager_t::Send(int dest,int message,void* data,int size) {
	logf << "Send to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	MPI_Send(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD);
}

void manager_t::ISend(int dest,int message) {
	logf << "ISend to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	//MPI_Isend(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, &mpi_request);
	MPI_Send(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD);
}

void manager_t::ISend(int dest,int message,void* data,int size) {
	logf << "ISend to " << dest << ": " << get_time() << " " << MSG_NAME[message] << endl;
	logf.flush();
	//MPI_Isend(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD, &mpi_request);
	MPI_Send(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD);
}

void manager_t::Recv(int dest,int message) {
	MPI_Recv(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void manager_t::Recv(int dest,int message,void* data,int size) {
	MPI_Recv(data, size, MPI_BYTE, dest, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

bool manager_t::IProbe(int& source,int& message_id) {
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

void manager_t::Non_Blocking_Send(int dest, int message) {
	mutex_lock(&lock_mpi);
	MPI_Isend(MPI_BOTTOM, 0, MPI_INT, dest, message, MPI_COMM_WORLD, &mpi_request);
	mutex_unlock(&lock_mpi);
}
