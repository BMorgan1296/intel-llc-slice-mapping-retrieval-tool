#include "uncore_address_map.h"

#include <perf_counters.h>
#include <perf_counters_util.h>
#include <string.h>


#define UNCORE_PERFMON_SAMPLES 10000

#define FIRST(k,n) ((k) & ((1<<(n))-1))
#define EXTRACT_BITS(k,m,n) FIRST((k)>>(m),((n)-(m)))

//evict mem[offset] into L3 and measure access time
//Requires huge pages to evict from L2
//returns minimum access time from 1000 samples
double get_slice_access_time(uint8_t *mem, uint64_t len, uint64_t offset)
{
	uint64_t t = 10000;
	uint64_t temp = 0;
	int cl_index_bits = find_set_bit(L2_CACHELINE);
	int l2_set_bits = find_set_bit(L2_SETS);
	register int mem_l2_set = EXTRACT_BITS((uint64_t)mem+offset, cl_index_bits, cl_index_bits + l2_set_bits);
	if(offset < len)
	{
		int samples = 10;
		for (int s = 0; s < samples; ++s)
		{
			clflush(mem+offset, 0);
			mfence();
			//initial access to bring into L1 cache
			mem[offset] = offset;
			mfence();
			#pragma GCC unroll 4096
			for (int i = 1; i <= (L1_ASSOCIATIVITY+L2_ASSOCIATIVITY)*8; ++i)
			{
				memaccess(&mem[(i * L2_STRIDE + (mem_l2_set * L2_CACHELINE)) % len]);
			}
			temp = (uint64_t)memaccesstime((void *)&mem[offset]);
			if(temp > 30 && t > temp)
			{
				t = temp;
			}
		}
		return (double)t;
	}
	else
	{
		return 0;
	}
}

int access_get_slice(uint8_t *mem, uint64_t len, uint64_t offset)
{
	if(offset >= len)
	{
		return -2;
	}

	int num_cbos = uncore_get_num_cbo(AFFINITY);	
	int num_proc = (int)sysconf(_SC_NPROCESSORS_ONLN);

	//Check if hyperthreading is enabled, if so, halve the number of cores
	int ht = 0;
	uint32_t registers[4];
	cpuid(&registers[0], &registers[1], &registers[2], &registers[3]);
	if(registers[3] & (1 << 28) || HT == 1)
	{
		ht = 1;
	}

	double *access_time = calloc(num_proc, sizeof(double));

	#define ACCESS_SAMPLES 10
	double access_min = 100000;

	for (int c = 0; c < num_proc; ++c)
	{
		double access_time_samples = 0;
		double access_min = 100000;

		set_cpu(c);

		//get the avg min from ACCESS_SAMPLES results
		for (int s = 0; s < ACCESS_SAMPLES; ++s)
		{
			access_time_samples += get_slice_access_time(mem, len, offset);
		}

		if(access_min >= (access_time_samples / (double)ACCESS_SAMPLES))
		{
			access_time[c] = access_time_samples / (double)ACCESS_SAMPLES;
			access_min = access_time[c];
		}
	}

	int access_slice = -1;
	double min = 100000;
	for (int c = 0; c < num_proc; ++c)
	{
		//Check to see if this is the minimum
		if(min >= access_time[c])
		{
			access_slice = c;
			//if hyperthreading is on, modulo by physical core count to get the actual slice no.
			if(ht)
				access_slice %= (num_proc/2);
			min = access_time[c];
		}
	}

	free(access_time);

	return access_slice;
}

int measure_slice_accesses(uncore_perfmon_t *u, uint8_t *mem, uint64_t len, uint64_t offset, int16_t *slice_res)
{
	//Need to implement averages
	int16_t total = 0;
	int slice_count = 0;

	int fail = 1;
	int found_slice_count = 0;
	int all_zeroes_count = 0;
	double mean, stddev;
	double *data = calloc(u->num_cbo_ctrs, sizeof(double));
	while(fail)
	{
		int index_zscore  = -1;
		int index_slice  = -1;
		int all_zeroes = 0;
		fail = 1;
		found_slice_count = 0;
		uncore_perfmon_monitor(u, clflush, (void *)&mem[offset], NULL);
		for (int s = 0; s < u->num_cbo_ctrs; ++s)
		{
			//Collect data for later zscore calculation
			data[s] = (double)u->results[s].total/(double)UNCORE_PERFMON_SAMPLES;
			//printf("%f\n", data[s]);

			//None of these values help us, reset.
			if(data[s] >= 2.0 || data[s] <= 0.0)
			{
		        fail = 1;
		        continue;
			}
			//These are the values we want.
			if(data[s] >= 1.0)
			{
				index_slice = s;
		        found_slice_count++;
			}
			else if (data[s] < 1.0)
			{
				all_zeroes++;
			}
		}
		//putchar('\n');
		//Check if we got all zeroes, therefore no CBos experienced CLFLUSHes
		if(all_zeroes == u->num_cbo_ctrs)
		{
			all_zeroes_count++;			
		}		
		//We have gotten all 0's too many times. Use timing.
		if(all_zeroes_count >= 1)
		{
			*slice_res = access_get_slice(mem, len, offset);
			fail = 0;
			break;
		}
		//Measurement fail, too noisy
		if(found_slice_count != 1)
		{
			fail = 1;
			continue;
		}
		else
		{
			// use z-scores on from the sample to check the values again. If there are more than one Z score either greater than 0 or less than -1,
			// then the range of values found is too large, and should be remeasured to get a more accurate result.
			// Expected is only 1 value is positively greater than 1 stddev away from the mean, and the rest should be within -1 stddevs away from the mean.
			int slice_not_found = 1;
			mean = calculate_mean(data, u->num_cbo_ctrs);
			stddev = calculate_stddev(data, u->num_cbo_ctrs, mean);

			int valid_zscore = 0;
			double zscore;
			for (int s = 0; s < u->num_cbo_ctrs; ++s)
			{
				zscore = calculate_zscore(data[s], mean, stddev);
				if(zscore > 0.0 || zscore < -1.0)
				{
					index_zscore = s;
					valid_zscore++;
				}
			}
			//If we only found 1 valid zscore
			if(valid_zscore == 1)
			{
				if(index_zscore == index_slice)
				{
					*slice_res = index_slice;
					fail = 0;
				}
				else
				{
					fail = 1;
				}
			}
		}
	}
	free(data);
}


int get_slice_value(uint8_t *mem, uint64_t len, uint64_t offset)
{
	//Setting scheduling to only run on a single core.
	uint8_t *ptr = &mem[offset];
	cpu_set_t mask;			
	CPU_ZERO(&mask);
	CPU_SET(AFFINITY, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if(result == -1)
	{
		perror("get_slice_values_adj()");
		exit(1);
	}

	int ret = 0;
	int16_t slice = 0;
	uncore_perfmon_t u;
	uint8_t num_cbos = uncore_get_num_cbo(AFFINITY);

	CBO_COUNTER_INFO_T *cbo_ctrs = malloc(num_cbos * sizeof(CBO_COUNTER_INFO_T));

	for (int i = 0; i < num_cbos; ++i)
	{
		COUNTER_T temp = {0x34, 0x8F, 0, "UNC_CBO_CACHE_LOOKUP.ANY_MESI"};
		cbo_ctrs[i].counter = temp;
		cbo_ctrs[i].cbo = i;
		cbo_ctrs[i].flags = (MSR_UNC_CBO_PERFEVT_EN);
	}

	uncore_perfmon_init(&u, AFFINITY, UNCORE_PERFMON_SAMPLES, num_cbos, 0, 0, cbo_ctrs, NULL, NULL);

	unsigned int pid = (unsigned int)getpid();
	
	*ptr = pid;
	measure_slice_accesses(&u, mem, len, offset, &slice);
	uncore_perfmon_destroy(&u);	//Destroy measurement util

	free(cbo_ctrs);
	
	CPU_ZERO(&mask);
	for (int i = 0; i < CORES; ++i)
	{		
		CPU_SET(i, &mask);
	}
	result = sched_setaffinity(0, sizeof(mask), &mask);
	if(result == -1)
	{
		perror("get_slice_values_adj()");
		exit(1);
	}
	return (int)slice;
}

void get_slice_values(uint8_t *mem, uint64_t len, uint64_t n_addr, uint64_t start_offset, int16_t *slice_map)
{
	uncore_perfmon_t u;
	uint8_t num_cbos = uncore_get_num_cbo(AFFINITY);

	CBO_COUNTER_INFO_T *cbo_ctrs = malloc(num_cbos * sizeof(CBO_COUNTER_INFO_T));

	for (int i = 0; i < num_cbos; ++i)
	{
		COUNTER_T temp = {0x34, 0x8F, 0, "UNC_CBO_CACHE_LOOKUP.ANY_MESI"};
		cbo_ctrs[i].counter = temp;
		cbo_ctrs[i].cbo = i;
		cbo_ctrs[i].flags = (MSR_UNC_CBO_PERFEVT_EN);
	}

	uncore_perfmon_init(&u, AFFINITY, UNCORE_PERFMON_SAMPLES, num_cbos, 0, 0, cbo_ctrs, NULL, NULL);

	unsigned int pid = (unsigned int)getpid();
	for (uint64_t i = start_offset; i < n_addr; ++i)
	{
		printf("%06ld/%06ld\r", i, n_addr);
		mem[i*L3_CACHELINE] = pid;
		measure_slice_accesses(&u, mem, len, (i*L3_CACHELINE), &slice_map[i]);
	}
	putchar('\n');
	uncore_perfmon_destroy(&u);	//Destroy measurement util

	free(cbo_ctrs);
}

int get_slice_values_adj(adj_addr_t *adj, uint8_t *mem, uint64_t len, uint64_t seq_len)
{
	//Setting scheduling to only run on a single core.
	cpu_set_t mask;			
	CPU_ZERO(&mask);
	CPU_SET(0, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if(result == -1)
	{
		perror("get_slice_values_adj()");
		exit(1);
	}

	int ret = 0;
	uncore_perfmon_t u;
	uint8_t num_cbos = uncore_get_num_cbo(AFFINITY);

	CBO_COUNTER_INFO_T *cbo_ctrs = malloc(num_cbos * sizeof(CBO_COUNTER_INFO_T));

	for (int i = 0; i < num_cbos; ++i)
	{
		COUNTER_T temp = {0x34, 0x8F, 0, "UNC_CBO_CACHE_LOOKUP.ANY_MESI"};
		cbo_ctrs[i].counter = temp;
		cbo_ctrs[i].cbo = i;
		cbo_ctrs[i].flags = (MSR_UNC_CBO_PERFEVT_EN);
	}

	uncore_perfmon_init(&u, AFFINITY, UNCORE_PERFMON_SAMPLES, num_cbos, 0, 0, cbo_ctrs, NULL, NULL);

	unsigned int pid = (unsigned int)getpid();
	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
	{
		for (uint64_t a = 0; a < NUM_ADJ_ADDR; ++a)
		{
			printf("Measuring Bit %02ld Adjacent Address Pair %03ld\r", b, a);
			//adjacent a addresses
			uint64_t start = adj->bit_n_a[b][a];
			uint64_t end = start + (seq_len*L3_CACHELINE);
			for (uint64_t i = start; i < end; i=i+L3_CACHELINE)
			{
				mem[i] = pid;
				measure_slice_accesses(&u, mem, len, i, &adj->slice_map_a[b][a][(i-start)/L3_CACHELINE]);
			}
			//adjacent b addresses
			start = adj->bit_n_b[b][a];
			end = start + (seq_len*L3_CACHELINE);
			for (uint64_t i = start; i < end; i=i+L3_CACHELINE)
			{
				mem[i] = pid;
				measure_slice_accesses(&u, mem, len, i, &adj->slice_map_b[b][a][(i-start)/L3_CACHELINE]);
			}
		}
	}
	printf("\n\n");
	uncore_perfmon_destroy(&u);	//Destroy measurement util
	free(cbo_ctrs);
		
	CPU_ZERO(&mask);
	for (int i = 0; i < CORES; ++i)
	{		
		CPU_SET(i, &mask);
	}
	result = sched_setaffinity(0, sizeof(mask), &mask);
	if(result == -1)
	{
		perror("get_slice_values_adj()");
		exit(1);
	}

	return ret;
}

//brute force search to find the ID which matches between sequence n and sequence 0
void fill_seq_data(sequence_data_t *seq_data, uint8_t *mem, int16_t *slice_map, uint64_t seq_len)
{
	for (int s = 0; s < NUM_SEQUENCES; ++s)
	{
		unsigned int pid = (unsigned int)getpid();
		int num_cbos = uncore_get_num_cbo(AFFINITY);
		mem[((s*L3_CACHELINE*seq_len))] = pid;
		uint64_t pa = vtop(pid, (uint64_t)&mem[((s*L3_CACHELINE*seq_len))]);
		seq_data[s].vaddr = (uint64_t)&mem[((s*L3_CACHELINE*seq_len))];
		seq_data[s].paddr = pa;
		memcpy(seq_data[s].sequence, slice_map+((s*seq_len)), seq_len*sizeof(int16_t));

		int fail = 0;
		for (uint64_t i = 0; i < MAX_ID; ++i)
		{
			fail = 0;
			for (uint64_t a = 0; a < seq_len; ++a)
			{	
				//Ignore not found slice values (may cause incorrect answers)
				//Also ignore slices outside the number of CBos (specifically for i9-10900K)
				if(slice_map[(s*seq_len)+a] >= num_cbos || slice_map[(s*seq_len)+a] == -1 || slice_map[a ^ i] == -1)
					continue;
				//does the current address in this sequence match the address in seq 0 ^ ID?
				if(slice_map[(s*seq_len)+a] != slice_map[a ^ i])
				{
					seq_data[s].xor_op = 0xBADBAD;
					fail = 1;
					break;
				}
			}
			if(!fail)
			{				
				seq_data[s].xor_op = i;
				break;
			}
		}
	}
}

//brute force search to find the ID which matches between sequence n and sequence 0
void fill_seq_data_adj(adj_addr_t *adj, uint8_t *mem, uint64_t seq_len)
{
	unsigned int pid = (unsigned int)getpid();
	int num_cbos = uncore_get_num_cbo(AFFINITY);
	//Get flag for if the current machine has power of 2 number of cores.
	int two_n_core_machine = is_power_of_two(uncore_get_num_cbo(AFFINITY));

	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
	{
		for (uint64_t a = 0; a < NUM_ADJ_ADDR; ++a)
		{
			//Accounts for offsets if bits within the 4KB page are set.
			int len = seq_len;
			//Copy bit_n_a info
			mem[adj->bit_n_a[b][a]] = pid;
			adj->seq_a[b][a].vaddr = (uint64_t)&mem[adj->bit_n_a[b][a]];
			adj->seq_a[b][a].paddr = vtop(pid, (uint64_t)&mem[adj->bit_n_a[b][a]]);
			for (int s = 0; s < len; ++s)
				adj->seq_a[b][a].sequence[s] = adj->slice_map_a[b][a][s];
			//Copy bit_n_b info
			mem[adj->bit_n_b[b][a]] = pid;
			adj->seq_b[b][a].vaddr = (uint64_t)&mem[adj->bit_n_b[b][a]];
			adj->seq_b[b][a].paddr = vtop(pid, (uint64_t)&mem[adj->bit_n_b[b][a]]);
			for (int s = 0; s < len; ++s)
				adj->seq_b[b][a].sequence[s] = adj->slice_map_b[b][a][s];

			//2^n machine, therefore can just XOR the two sequences together.
			if(two_n_core_machine)
			{
				//Only need to do this once, but we ignore -1 values anyway.
				for (int addr = 0; addr < seq_len; ++addr)
				{
					if(adj->slice_map_a[b][a][addr] == -1 || adj->slice_map_b[b][a][addr] == -1)
						continue;
					//ID is equal to the result of XORing the two adjacent address sequences together
					adj->seq_a[b][a].xor_op = adj->slice_map_a[b][a][addr] ^ adj->slice_map_b[b][a][addr];
					//B becomes 0 as A will hold the actual XOR ID that this sequence needs.
					adj->seq_b[b][a].xor_op = 0;
					break;
				}
			}
			//Not 2^n machine, therefore get XOR offset
			else
			{
				for (uint64_t i = 0; i < MAX_ID; ++i)
				{
					int fail = 0;
					for (int addr = 0; addr < seq_len; ++addr)
					{
						//Ignore not found slice values (may cause incorrect answers)
						if(adj->slice_map_a[b][a][addr] >= num_cbos || adj->slice_map_a[b][a][addr] == -1 || adj->slice_map_a[START_BIT(seq_len)][0][addr ^ i] == -1)
							continue;
						//does the current address in this sequence match the address in seq ADDR_BITS-1 XOR ID?
						if(adj->slice_map_a[b][a][addr] != adj->slice_map_a[START_BIT(seq_len)][0][addr ^ i])
						{			
							adj->seq_a[b][a].xor_op = 0xBADBAD;			
							fail = 1;
							break;
						}
					}
					if(!fail)
					{
						adj->seq_a[b][a].xor_op = i;
						break;
					}				
				}			
				//Now do it for the other 'b' sequences.
				for (uint64_t i = 0; i < MAX_ID; ++i)
				{
					int fail = 0;
					for (int addr = 0; addr < seq_len; ++addr)
					{
						//Ignore not found slice values (may cause incorrect answers)
						if(adj->slice_map_b[b][a][addr] >= num_cbos || adj->slice_map_b[b][a][addr] == -1 || adj->slice_map_a[START_BIT(seq_len)][0][addr ^ i] == -1)
							continue;
						if(adj->slice_map_b[b][a][addr] != adj->slice_map_a[START_BIT(seq_len)][0][addr ^ i])
						{			
							adj->seq_b[b][a].xor_op = 0xBADBAD;			
							fail = 1;
							break;
						}
					}
					if(!fail)
					{
						adj->seq_b[b][a].xor_op = i;
						break;
					}				
				}
			}
		}
	}
}

void print_slice_values(sequence_data_t *seq_data, uint64_t seq_len)
{
	for (int i = 0; i < NUM_SEQUENCES; ++i)
	{
		printf("Px%011lx | ", seq_data[i].paddr);
		for (int s = 0; s < seq_len; ++s)
		{
			if(seq_data[i].sequence[s] >= 0)
				printf("%d", seq_data[i].sequence[s]);
			else
				printf("_");
			if(s % 4 == 3)
				putchar(' ');
		}
		printf(" | 0x%04lx\n", seq_data[i].xor_op);
	}
}

void print_slice_values_adj(adj_addr_t *adj, uint8_t *mem, uint64_t seq_len)
{
	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
	{
		for (int a = 0; a < NUM_ADJ_ADDR; ++a)
		{
			//Print A
			printf("Bit %ld Addr %d\n", b, a);
			printf("0x%011lx | Px%011lx | ", adj->bit_n_a[b][a], adj->seq_a[b][a].paddr);
			for (int s = 0; s < seq_len; ++s)
			{
				if(adj->seq_a[b][a].sequence[s] >= 0)
					printf("%d", adj->seq_a[b][a].sequence[s]);
				else
					printf("X");
				if(s % 4 == 3)
					putchar(' ');
			}
			printf(" | 0x%04lx\n", adj->seq_a[b][a].xor_op);

			//print B
			printf("0x%011lx | Px%011lx | ", adj->bit_n_b[b][a], adj->seq_b[b][a].paddr);
			for (int s = 0; s < seq_len; ++s)
			{
				if(adj->seq_b[b][a].sequence[s] >= 0)
					printf("%d", adj->seq_b[b][a].sequence[s]);
				else
					printf("X");
				if(s % 4 == 3)
					putchar(' ');
			}
			printf(" | 0x%04lx\n", adj->seq_b[b][a].xor_op);
			putchar('\n');
		}
	}
}

uint64_t calculate_xor_reduction(uint64_t addr, int xor_map[ADDR_BITS])
{
	//Check if any bits are set past ADDR_BITS
	if((addr >> ADDR_BITS) > 0)
		return -1;
	uint64_t xor_op = 0;

	for (int b = 0; b < ADDR_BITS; ++b)
	{
		if(is_bit_k_set(addr, b))
			xor_op ^= (uint64_t)xor_map[b];
	}
	return xor_op;
}

int calculate_address_slice(uint64_t paddr, int16_t *master_sequence, uint64_t seq_len, int xor_map[ADDR_BITS])
{
	int calc_slice = -1;
	int xor_op = -1;

	xor_op = calculate_xor_reduction(paddr, xor_map);

	//Get the slice of the address's sequence offset within the master sequence
	if(xor_op >= 0)
	{
		if(master_sequence == NULL)
		{
			calc_slice = xor_op;
		}
		else
		{
			int sequence_offset = ((uint64_t)paddr % (seq_len*L3_CACHELINE)) >> 6;
			calc_slice = master_sequence[sequence_offset ^ xor_op];			
		}
	}
	else
	{
		calc_slice = -1;
	}
	return calc_slice;
}
