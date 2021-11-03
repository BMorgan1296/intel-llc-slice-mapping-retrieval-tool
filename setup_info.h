#ifndef SETUP_INFO_H
#define SETUP_INFO_H

//Affinity to run the uncore performance counter interface on
//Can isolate the core then run this tool on it
#define AFFINITY 0

//For viewing slice mapping with different amounts of sequences
#ifndef NUM_SEQUENCES
	#define NUM_SEQUENCES 16
#endif

//Can choose which bits to start searching for. Good for debugging or getting partial slice mapping info.
//#define START_BIT(x) 30

//How many adjacent addresses we want for each memory address bit.
#define NUM_ADJ_ADDR 2

#endif //SETUP_INFO_H
