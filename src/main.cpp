#include <iostream>

#include "viper.h"
#include "cluster.h"



using namespace std;

int main1(void) {
  // No buffering, please...
  setbuf(stdout, NULL);
  setbuf(stdin, NULL);
  // and I *really* mean it, too!
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stdin, NULL, _IONBF, 0);

  init();
  uci_main_loop();
  return 0;
}

#if defined(USE_CLUSTER_SEARCH)
void print_pid()
{
	int i = 0, j = 0;
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("PID %d on %s ready for attach\n", getpid(), hostname);
    fflush(stdout);
    //while (i == 0) {
    //    sleep(1);
	//	j = 1;
	//	i = 0;
	//}
}
#endif


// host
#if defined(USE_CLUSTER_SEARCH)
host_t my_local_host;
#endif

int main(int argc, char* argv[])
{
	int rank, size;
	int ierr, i, j;
#if defined(USE_CLUSTER_SEARCH)
	host_option_t host_opt;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	//MPI_Get_processor_name(PROCESSOR::host_name, &namelen);
	//print("Process [%d/%d] on %s : pid %d\n",PROCESSOR::host_id,
	//PROCESSOR::n_hosts,PROCESSOR::host_name, GETPID());
	MPI_Status status;

	// what's my pid?
	print_pid();
#endif

	  // No buffering, please...
	setbuf(stdout, NULL);
	setbuf(stdin, NULL);
	// and I *really* mean it, too!
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	init();

	// main processes
	if (rank == 0) {
/*
		string cmd;
		int des;
		init_message_t init_msg;
		split_message_t split_msg;

		while (true) {
			cin >> cmd;
			cin >> des;

			if (cmd == "split") {
				split_msg.depth = 3;//(rand() % 3);
				MPI_Isend(&split_msg, sizeof(split_message_t), MPI_BYTE, des, SPLIT, MPI_COMM_WORLD, &(local_host.mpi_request));

				//local_host.Non_Blocking_Send(des, SPLIT);
			} else if (cmd == "init") {
				memcpy(&(init_msg.init_pos), &main_pos, sizeof(board_t));
				MPI_Isend(&init_msg, sizeof(init_message_t), MPI_BYTE, des, INIT, MPI_COMM_WORLD, &(local_host.mpi_request));

			} else if (cmd == "quit") {
				for (j = 0; j < size; j++) {
					local_host.Non_Blocking_Send(j, QUIT);
				}
				cout << "Main process quit!" << endl;
				break;
			}
		}
*/

#if defined(USE_CLUSTER_SEARCH)
		host_opt.host_id = rank;
		host_opt.n_host  = size;
		host_opt.n_thread = 1;
		// init host
		my_local_host.initialize(host_opt);

		// start main work
		my_local_host.main_host_work();
#else
		// start uci loop!
		uci_main_loop();
#endif

	//} else if (rank == (size)) {


	// other processes
	} else {
#if defined(USE_CLUSTER_SEARCH)
		split_point_t *empty_sp;
		empty_sp = NULL;

		host_opt.host_id = rank;
		host_opt.n_host  = size - 1;
		host_opt.n_thread = 1;
		// init host
		my_local_host.initialize(host_opt);

		// host begin to idle loop!
		my_local_host.host_idle_loop(*empty_sp);
#endif
	}

	cout << "Bye~" << endl;

#if defined(USE_CLUSTER_SEARCH)
	int err = MPI_Finalize();
	//cout << "[" << my_local_host.host_id << "] FinalizeID = " << err << endl;
#endif

	return 0;
}
