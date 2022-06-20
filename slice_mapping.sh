#!/bin/bash

find_set_bit()
{
	TEMP=$1
	COUNT=0
	while [[ TEMP -ne 1 ]]; do
		TEMP=$(($TEMP/2))
		COUNT=$((COUNT+1))
	done
	return $COUNT
}

is_power_of_two()
{
	TEMP=$1
	TEMP=$(($1 & $(($1 - 1))))
	if [[ $TEMP -eq 0 ]]; then
		return 1
	else
		return 0
	fi
}

#CPU Cores info
CORES=$(grep -c ^processor /proc/cpuinfo)
HT=$(lscpu | grep Thread | awk '{print $4}')
NUM_THREADS=$(($CORES/$HT))

#Memory info
RAM=$(awk '/MemTotal/{print $2}' /proc/meminfo)
RAM=$(($RAM*1024))
find_set_bit $RAM
ADDR_BITS=$(($?+1))
RAM=$(($RAM/8))
PORTION=7
RAM=$(($RAM*$PORTION))

#Paging info
find_set_bit $(awk '/Hugepagesize/{print $2}' /proc/meminfo)
PAGE_BITS=$?

#CPU L1 Info
L1D=$(getconf -a | grep L1_DCACHE_SIZE | awk '{print $2}')
L1_ASSOCIATIVITY=$(getconf -a | grep LEVEL1_DCACHE_ASSOC | awk '{print $2}')
L1_CACHELINE=$(getconf -a | grep LEVEL1_DCACHE_LINESIZE | awk '{print $2}')

#CPU L2 Info
L2=$(getconf -a | grep L2_CACHE_SIZE | awk '{print $2}')
L2_ASSOCIATIVITY=$(getconf -a | grep LEVEL2_CACHE_ASSOC | awk '{print $2}')
L2_CACHELINE=$(getconf -a | grep LEVEL2_CACHE_LINESIZE | awk '{print $2}')

#CPU L3 Info
L3_ASSOCIATIVITY=$(getconf -a | grep LEVEL3_CACHE_ASSOC | awk '{print $2}')
L3_CACHELINE=$(getconf -a | grep LEVEL3_CACHE_LINESIZE | awk '{print $2}')

if [[ $1 = "--view" ]]; then
	#Enable huge pages and MSR interacton
	sudo modprobe msr
	echo 65536 | sudo tee /proc/sys/vm/nr_hugepages
	if [[ $(sudo cat /proc/sys/vm/nr_hugepages) -eq 0 ]]; then
		echo "Could not create hugepages, check system settings. Exiting."
		exit 0
	fi
	#Compile
	sudo make clean
	sudo make view_slice_mapping OPS="-DCORES=$CORES -DHT=$HT -DNUM_THREADS=$NUM_THREADS -DRAM=$RAM -DADDR_BITS=$ADDR_BITS -DUSEHUGEPAGE -DL1D=$L1D -DL1_ASSOCIATIVITY=$L1_ASSOCIATIVITY -DL1_CACHELINE=$L1_CACHELINE -DL2=$L2 -DL2_ASSOCIATIVITY=$L2_ASSOCIATIVITY -DL2_CACHELINE=$L2_CACHELINE -DL3_CACHELINE=$L3_CACHELINE"
	echo
	#Run with every core available
	date
	sudo chrt -r 1 sudo taskset -c 0-$(($CORES-1)) ./view_slice_mapping
	date
	#Turn off huge pages
	echo 0 | sudo tee /proc/sys/vm/nr_hugepages
elif [[ $1 = "--get" ]]; then
	#Enable huge pages and MSR interacton
	sudo modprobe msr
	echo 65536 | sudo tee /proc/sys/vm/nr_hugepages
	if [[ $(sudo cat /proc/sys/vm/nr_hugepages) -eq 0 ]]; then
		echo "Could not create hugepages, check system settings. Exiting."
		exit 0
	fi
	SEQ_LEN=128
	# is_power_of_two $CORES
	# if [[ $? -eq 1 ]]; then
	# 	echo "Sequence length is 1 due to power of 2"
	# 	SEQ_LEN=1
	# else
	# 	echo "Finding sequence length"
	# 	#Compile
	# 	sudo make clean
	# 	sudo make view_slice_mapping OPS="-DNUM_SEQUENCES=64 -DCORES=$CORES -DHT=$HT -DNUM_THREADS=$NUM_THREADS -DRAM=$RAM -DADDR_BITS=$ADDR_BITS -DUSEHUGEPAGE -DL1D=$L1D -DL1_ASSOCIATIVITY=$L1_ASSOCIATIVITY -DL1_CACHELINE=$L1_CACHELINE -DL2=$L2 -DL2_ASSOCIATIVITY=$L2_ASSOCIATIVITY -DL2_CACHELINE=$L2_CACHELINE -DL3_CACHELINE=$L3_CACHELINE"
	# 	#Getting SEQ_LEN by parsing view_slice_mapping
	# 	SEQ_LEN=$(sudo chrt -r 1 sudo taskset -c 0-$(($CORES-1)) ./view_slice_mapping | grep "Max Sequence Length:" | awk '{print $4}')
	# 	echo "Sequence length found: $SEQ_LEN"
	# fi

	#Run with every core available
	#Reduce RAM until we don't fail on allocating memory, starting from 15/16 of total system RAM and reducing this by 1/16 each time until it works
	RES=1
	while [[ $RES -ne 0 ]]; do
		#Compile
		echo "Compiling with $RAM RAM"
		sudo make clean
		sudo make get_slice_mapping OPS="-DSEQ_LEN=$SEQ_LEN -DCORES=$CORES -DHT=$HT -DNUM_THREADS=$NUM_THREADS -DRAM=$RAM -DADDR_BITS=$ADDR_BITS -DUSEHUGEPAGE -DL1D=$L1D -DL1_ASSOCIATIVITY=$L1_ASSOCIATIVITY -DL1_CACHELINE=$L1_CACHELINE -DL2=$L2 -DL2_ASSOCIATIVITY=$L2_ASSOCIATIVITY -DL2_CACHELINE=$L2_CACHELINE -DL3_CACHELINE=$L3_CACHELINE"
		echo
		if [[ $2 = "--save" ]]; then
		 	MODEL=$(lscpu | grep "Intel" | awk -F "Intel" '{print $2}' | awk -F " " '{print $3}')
		 	MODEL=$(printf "%s" $MODEL)
		 	DATE=$(echo -n $(date +"%s"))
		 	OF=$(printf "./output/%s_%s.txt\n" $MODEL $DATE)
		 	echo "Saving output to $OF"
		 	#Saving some extra info at start of file
		 	echo "Model: $MODEL" >> $OF
		 	echo "Cores: $CORES" >> $OF
		 	echo "Time: $DATE" >> $OF
		 	echo "Total RAM: $(awk '/MemTotal/{print $2}' /proc/meminfo)" >> $OF
		 	echo "Physical Address Bits: $ADDR_BITS" >> $OF
		 	echo >> $OF
		 	echo "L1 Info" >> $OF
		 	echo "L1D Size: $L1D" >> $OF
		 	echo "L1D Associativity: $L1_ASSOCIATIVITY" >> $OF
		 	echo "L1D Cacheline: $L1_CACHELINE" >> $OF
		 	echo >> $OF
		 	echo "L2 Info" >> $OF
		 	echo "L2 Size: $L2" >> $OF
		 	echo "L2 Associativity: $L2_ASSOCIATIVITY" >> $OF
		 	echo "L2 Cacheline: $L2_CACHELINE" >> $OF
		 	echo >> $OF
		 	echo "L3 Info" >> $OF
		 	echo "L3 Associativity: $L3_ASSOCIATIVITY" >> $OF
		 	echo "L3 Cacheline: $L3_CACHELINE" >> $OF
		 	echo "------------------------------------------------" >> $OF
		 	#Run the tool
		 	sudo chrt -r 1 sudo taskset -c 0-$(($CORES-1)) ./get_slice_mapping >> $OF
			RES=$?
			#Delete the output file if tool failed
			if [[ $RES -ne 0 ]]; then
				rm $OF
			fi
		else
			echo
		 	date &&	sudo chrt -r 1 sudo taskset -c 0-$(($CORES-1)) ./get_slice_mapping && date
			RES=$?
		fi
		RAM=$(($RAM/$PORTION))
		RAM=$(($RAM*16))
		if [[ $PORTION -gt 1 ]]; then
		 	PORTION=$(($PORTION-1))
		else
			echo "Error: Cannot run slice retrieval tool, memory mapping issue"
			RES=0
		fi
		RAM=$(($RAM/16))
		RAM=$(($RAM*$PORTION))
	done
	echo "Done"
    #Turn off huge pages
	echo 0 | sudo tee /proc/sys/vm/nr_hugepages
else
	echo "Usage: sudo ./slice_mapping.sh --[view|get] [--save]"
fi
