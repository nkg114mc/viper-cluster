# Viper-Cluster

Viper-Cluster is a chess [perft](https://chessprogramming.wikispaces.com/Perft) program that is running on the cluster system. It starts from the source code of chess engine Viper, and also borrowed lots of ideas and code from chess engine Scorpio. My initial goal of this program is to implement a cluster based parallel search framework as a practice of programming for the MPI based parallel alpha-beta search. The most of my work is in cluster.cpp and main.cpp.

## Prerequisite

Although derived from Viper, which can be compiled and run on different operating systems, Viper-Cluster currently is only developed and tested under Linux environment, or more specifically, Ubuntu and CentOS.

To run a MPI application, you need to install a MPI implementation. In this program, I use MPICH3. To install MPICH3, you either download the MPICH official website to install from source code, or if you are under Ubuntu, simply type the command below:

	sudo apt-get install libcr-dev mpich mpich-doc

## Compile


To compile the program, cd to src directory, then use make to compile

	make -f Makefile.cluster

or simple run script ./compli.sh

## Run

So far, the input perft position (fen string) and depth are all hard coded in the cluster.cpp line 1480 and 1481.

	char fenstr[256] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
	depth = 7;

As show above, the default position is just starting position, and default depth is 7. You can change to any position and depth you want. Don't forget to re-compile before running.

To run perft, cd to the directory where file **viper** is located. Then type following command to start running with command

	mpiexec -np <Number-of-Processes> ./viper

where _Number-of-Processes_ is just the number of processes (hosts) you want.
**Note** that, Viper-Cluster is using centralized controlling (master-slave architecture). If the processes are indexed from 0 to (_Number-of-Processes_ - 1), then the last process, indexed by _Number-of-Processes_ - 1, will just do management, no computing. Thus, if you setup N processes, only N - 1 are worker processes that do perft computing. And, if you want N' computing workers, remember to set _Number-of-Processes_ be N' + 1.

When the perft finished, it will generate a file named "result.txt", which shows the perft result. Each process will also print a brief summary on the screen about how many nodes were searched, and a more detailed log named "host-i.log", where i is the process index. For example, when using the default input position and depth, the result would be like:

	======== Performance Testing depth = 7 ========
	 position: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
	 perft = 3195901860
	 time cost (ms) = 68769
	-------- Performance Testing depth = 7 --------

## Acknowledgement

I would like to appreciate Mr. Romstad (Tord Romstad) who developed the engine [Viper](https://chessprogramming.wikispaces.com/Viper), and Mr. Shawul (Daniel Shawul) who developed the engine [Scorpio](https://sites.google.com/site/dshawul/home). Without their excellent prior work, it might take me much longer time to learn the concepts in parallel search, and build up this program.

