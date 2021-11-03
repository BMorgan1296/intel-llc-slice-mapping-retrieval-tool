#include "uncore_address_map.h"
#include <string.h>
#include <math.h>

//Holds offset for adjacent addresses in the mmap buffer, which differ by one bit.
//Holds NUM_ADJ_ADDR offsets for each address bit (0-40) as well the current count for that bit.

struct search_for_bit_n_args
{
	uint64_t thread_id;
	uint64_t pid;
	uint64_t seq_len;
	//memory info. Buffer, physical address to 
	//search for adjacent line at a certain bit.
	uint8_t *mem;
	uint64_t offset;
	//Search space
	uint64_t start;
	uint64_t end;
	uint64_t it;
	adj_addr_t *adj;
};

volatile int search_for_bit_n_threads = 0;
pthread_mutex_t search_for_bit_n_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t adjacent_addr_struct_mutex = PTHREAD_MUTEX_INITIALIZER;

//Just searching through RAM for an address with 34th bit set (might not exist)
//Multithreaded search from start bytes to end bytes of mmap'd buffer
//One 'master' thread will find an address with a specified bit n which is set.
//When this is found, the other threads will search for addresses which are adjacent.
//These threads will start at equidistant intervals within the search space, such that they don't overlap.
//Adjacent addresses will be stored in bit_n_a and bit_n_b using mutexes with count incremented.
void *search_for_adjacent_bit_n(void *targs)
{
	struct search_for_bit_n_args args = *(struct search_for_bit_n_args *)targs;
	register uint8_t *ptr = args.mem;
	register uint64_t pid = args.pid;
	register uint64_t seq_len = args.seq_len;
	register uint64_t p_addr = vtop(pid, (uint64_t)&ptr[args.offset]);
	register uint64_t bit = 0;
	register uint64_t end = args.end;
	register uint64_t it = args.it;

	//Required for vtop()
	register uint64_t pa = 0LL;

	//search from provided start point, incrementing by cache line
	for (uint64_t i = args.start; i < end; i=i+it)
	{
		ptr[i] = pid;
		pa = vtop(pid, (uint64_t)&ptr[i]);
		bit = does_val_differ_by_one(p_addr, pa);
		if(bit)
		{
			if(pa != p_addr)
			{
				pthread_mutex_lock(&adjacent_addr_struct_mutex);
					if(bit >= START_BIT(seq_len) && bit < ADDR_BITS)
					{
						if(args.adj->count[bit] < NUM_ADJ_ADDR)
						{
							args.adj->bit_n_a[bit][args.adj->count[bit]] = args.offset; //given physical address
							args.adj->bit_n_b[bit][args.adj->count[bit]] = i; //found physical address
							args.adj->count[bit]++;
							printf("Bit: %02ld | %d/%d | 0x%011lx (%011lx) | 0x%011lx (%011lx)\n", bit, args.adj->count[bit], NUM_ADJ_ADDR, p_addr, args.offset, pa, i);
						}
					}

				pthread_mutex_unlock(&adjacent_addr_struct_mutex);
			}			
		}
	}
	free(targs);
	pthread_mutex_lock(&search_for_bit_n_mutex);
	search_for_bit_n_threads--;
	pthread_mutex_unlock(&search_for_bit_n_mutex);
	pthread_exit(NULL);
}

void *search_for_adjacent_bit_n_intra_page(void *targs)
{
	struct search_for_bit_n_args args = *(struct search_for_bit_n_args *)targs;
	register uint8_t *ptr = args.mem;
	register uint64_t pid = args.pid;
	register uint64_t seq_len = args.seq_len;	
	register uint64_t p_addr = vtop(pid, (uint64_t)&ptr[args.offset]);
	register uint64_t bit = 0;
	register uint64_t end = args.end;
	register uint64_t it = args.it;

	//Required for vtop()
	register uint64_t pa = 0LL;

	//search from provided start point, incrementing by cache line
	for (uint64_t i = args.start; i < end; i=i+it)
	{
		ptr[i] = pid;
		pa = vtop(pid, (uint64_t)&ptr[i]);
		bit = does_val_differ_by_one(p_addr, pa);
		if(bit)
		{
			if(pa != p_addr)
			{
				if(bit >= START_BIT(seq_len) && bit < ADDR_BITS)
				{
					if(args.adj->count[bit] < NUM_ADJ_ADDR)
					{
						args.adj->bit_n_a[bit][args.adj->count[bit]] = i; //found physical address
						args.adj->bit_n_b[bit][args.adj->count[bit]] = args.offset; //provided physical address with bit n set
						args.adj->count[bit]++;
						printf("Bit: %02ld | %d/%d | 0x%011lx (%011lx) | 0x%011lx (%011lx)\n", bit, args.adj->count[bit], NUM_ADJ_ADDR, p_addr, args.offset, pa, i);
					}
				}
			}
		}
	}
}

void adjacent_address_search(adj_addr_t *adj, uint8_t *mem, uint64_t len, uint64_t seq_len)
{
	unsigned int pid = (unsigned int)getpid();

	for (int i = 0; i < ADDR_BITS; ++i)
	{
		adj->count[i] = 0;
	}

	pthread_t threads[NUM_THREADS];
	uint64_t pa = 0LL;
	
	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS;)
	{
		if(adj->count[b] == NUM_ADJ_ADDR)
		{
			//printf("Found enough adjacent addresses for bit %d (%d)\n", b, adj->count[b]);
			b++;
			continue;
		}
		//printf("Searching for %d adjacent addresses of bit %d | %d\n", NUM_ADJ_ADDR, b, PAGE_BITS);
		//Only search half of RAM, but allocate the entire lot.

		uint64_t it = L3_CACHELINE;

		for (uint64_t i = 0; i < len; i=i+it)
		{
			//Change iterator if we are searching below page size.
			if(b < PAGE_BITS)
			{
				it = (1ULL<<b);
			}
			else
				it = PAGE_SIZE;

			mem[i] = pid;
			pa = vtop(pid, (uint64_t)&mem[i]);

			//Checks if bit is not set, such that adj_addr_a hold the address with the bit not set
			if(is_bit_k_set(pa, b))
			{
				//printf("Looking for %d addresses adjacent on bit %d to 0x%011lx, iterating by 0x%lx\n", NUM_ADJ_ADDR-adj->count[b], b, pa, it);
				if(b < PAGE_BITS)
				{
					struct search_for_bit_n_args *args = malloc(sizeof(struct search_for_bit_n_args));
					args->thread_id = 0;
					args->pid = pid;
					args->seq_len = seq_len;
					args->mem = mem;
					args->offset = i;
					args->start = i - (i % PAGE_SIZE); //start searching from start of page
					args->end = i + PAGE_SIZE;
					args->it = L3_CACHELINE;					
					args->adj = adj;

					search_for_adjacent_bit_n_intra_page((void *)args);
					free(args);
					//Go to next page
					i += PAGE_SIZE;
					i -= (i % PAGE_SIZE);
				}
				else
				{
					for(int t = 0; t < NUM_THREADS; t++ )
					{
						cpu_set_t mask;			
						CPU_ZERO(&mask);
						CPU_SET(t, &mask);
						struct search_for_bit_n_args *args = malloc(sizeof(struct search_for_bit_n_args));
						args->thread_id = t;
						args->pid = pid;
						args->seq_len = seq_len;
						args->mem = mem;
						args->offset = i;
						args->start = (t * (len/NUM_THREADS));
						args->end = (t * (len/NUM_THREADS)) + (len/NUM_THREADS);
						args->it = PAGE_SIZE;					
						args->adj = adj;

						int err = pthread_create(&threads[t], NULL, search_for_adjacent_bit_n, (void *)args);
						if(err) 
						{
							printf("Error: unable to create thread: %d\n", err);
							exit(1);
						}
						if (pthread_setaffinity_np(threads[t], sizeof(cpu_set_t), &mask) == -1)
						{
							perror("sched_getaffinity");
							exit(1);
						}

				   		pthread_mutex_lock(&search_for_bit_n_mutex);
							search_for_bit_n_threads++;
				   		pthread_mutex_unlock(&search_for_bit_n_mutex);
					}
					//Wait for threads to end
					while(search_for_bit_n_threads > 0)
					{
						sleep(1);
						if(adj->count[b] == NUM_ADJ_ADDR)
						{
							//Join the threads up to finish.
							for (int t = 0; t < NUM_THREADS; ++t)
								pthread_join(threads[t], NULL);
						}
					}
				}
			}
			if(adj->count[b] == NUM_ADJ_ADDR)
			{				
				break;
			}
		}
	}
}

void find_xor_for_each_bit(adj_addr_t *adj, int xor_map[ADDR_BITS], uint64_t seq_len)
{
	double xor_map_unrounded[ADDR_BITS] = {0.0};
	int xor_map_xor_count[ADDR_BITS] = {0};

	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
	{
		for (int a = 0; a < NUM_ADJ_ADDR; ++a)
		{
			if(adj->seq_a[b][a].xor_op != adj->seq_b[b][a].xor_op && adj->seq_a[b][a].xor_op != 0xBADBAD && adj->seq_b[b][a].xor_op != 0xBADBAD)
			{
				xor_map_unrounded[b] += (double)(adj->seq_a[b][a].xor_op ^ adj->seq_b[b][a].xor_op);
				xor_map_xor_count[b]++;
			}			
		}
	}

	for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
	{
		if(xor_map_xor_count[b] > 0)
			xor_map[b] = (int)round(xor_map_unrounded[b]/(double)xor_map_xor_count[b]);
		else
			xor_map[b] = 0;
		printf("Bit: %02ld | Total: %03d | Count: %02d | ID_Rounded: 0x%x | ID: %f\n", b, (int)xor_map_unrounded[b], xor_map_xor_count[b], xor_map[b], (xor_map_unrounded[b]/(double)xor_map_xor_count[b]));
	}
	putchar('\n');
}

void find_master_sequence(adj_addr_t *adj, uint8_t *mem, uint64_t len, int16_t *master_sequence, uint64_t seq_len, int xor_map[ADDR_BITS])
{
	unsigned int pid = (unsigned int)getpid();
	//Get an average of ID->Slice values, which will show a reduction on non power of two processors 
	//from the larger ID space to the smaller number of possible slice values.
	int16_t *id_to_slice_map = calloc(seq_len, sizeof(int16_t));
	uint64_t *id_to_slice_map_count = calloc(seq_len, sizeof(uint64_t));

	int incomplete = 0;

	//Get a sample of addresses which start at the beginning of a sequence and their slice mappings
	//This should be 1:1 and show the reduction used to go from n sequence IDs to m slices.
	//Collect 10 samples for each ID. Select the slice mapping as the returned ID which occured the most.
	for (int i = 0; i < seq_len*8; ++i)
	{
		uint64_t rnd = (uint64_t) rand() % len;
		//Get random value which is at the start of a sequence
		rnd -= (rnd % (seq_len*L3_CACHELINE));

		//Bring the address into memory
		mem[rnd] = pid;

		int id = calculate_xor_reduction(vtop(pid, (uint64_t)&mem[rnd]), xor_map);
		int slice = get_slice_value(mem, len, rnd);
		//Ignore -1 results, don't have enough bits to resolve these
		if(id != -1)
		{
			id_to_slice_map[id] += slice;
			id_to_slice_map_count[id]++;
			//Ignore if we have a -1 here, as this means we have had issues with this ID previous
			if(id_to_slice_map[id] > 0)
			{
				if((id_to_slice_map[id] % id_to_slice_map_count[id]) != 0)
				{
					id_to_slice_map[id] = -1;				
				}
			}			
		}
	}
	//Get the master sequence. If there are any portions missing, we check it with all the 
	//adjacent address sequences to find the one that matches the most.
	for (int i = 0; i < seq_len; ++i)
	{
		if(id_to_slice_map_count[i] > 0)
		{
			id_to_slice_map[i] /= id_to_slice_map_count[i];
			master_sequence[i] = id_to_slice_map[i];
		}
		else
		{
			id_to_slice_map[i] = -1;
			incomplete = 1;
		}
	}
	//Master sequence may be incomplete, therefore look for a sequence in the adjacent addresses which is most similar
	//The most similar will be the master sequence.
	if(incomplete)
	{
		int max_mem_cmp = -10000;
		for (uint64_t b = START_BIT(seq_len); b < ADDR_BITS; ++b)
		{
			for (uint64_t a = 0; a < NUM_ADJ_ADDR; ++a)
			{
				int cmp = memcmp(id_to_slice_map, adj->slice_map_a[b][a], seq_len*sizeof(int16_t));
				if(cmp >= max_mem_cmp)
				{
					max_mem_cmp = cmp;
					memcpy(master_sequence, adj->slice_map_a[b][a], seq_len*sizeof(int16_t));
				}
			}
			for (uint64_t a = 0; a < NUM_ADJ_ADDR; ++a)
			{
				int cmp = memcmp(id_to_slice_map, adj->slice_map_b[b][a], seq_len*sizeof(int16_t));
				if(cmp >= max_mem_cmp)
				{
					max_mem_cmp = cmp;
					memcpy(master_sequence, adj->slice_map_b[b][a], seq_len*sizeof(int16_t));
				}
			}
		}
	}
	free(id_to_slice_map);
	free(id_to_slice_map_count);
}

adj_addr_t *adjacent_address_init()
{
	adj_addr_t *temp = calloc(1, sizeof(adj_addr_t));
	memset(temp, 0, sizeof(adj_addr_t));
	return temp;
}

void adjacent_address_destroy(adj_addr_t *adj)
{
	free(adj);
}