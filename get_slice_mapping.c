#include "uncore_address_map.h"
#include <perf_counters.h>
#include <string.h>

int main(int argc, char const *argv[])
{
	int ret = 0;
	unsigned int pid = (unsigned int)getpid();
	int num_cbos = uncore_get_num_cbo(AFFINITY);
	size_t len = (size_t)RAM;
	uint8_t *mem = mmap(NULL, sizeof(uint8_t) * len, PROT_READ | PROT_WRITE | PROT_EXEC, MMAP_FLAGS, -1, 0);
	if((size_t)mem == -1)
	{
		perror("get_slice_mapping()");
		exit(1);
	}
	adj_addr_t *adj = adjacent_address_init();
	int xor_map[ADDR_BITS] = {0};
	int16_t *master_sequence = calloc(SEQ_LEN, sizeof(int16_t));

	printf("Sequence length is %d cache lines\n", SEQ_LEN);

	adjacent_address_search(adj, mem, len, SEQ_LEN);
	putchar('\n');

	//Get slice values from the perf counter library
	ret = get_slice_values_adj(adj, mem, len, SEQ_LEN);

	//Fill the sequence data with info from the slice mapping
	fill_seq_data_adj(adj, mem, SEQ_LEN);

	//Print each sequence
	print_slice_values_adj(adj, mem, SEQ_LEN);

	find_xor_for_each_bit(adj, xor_map, SEQ_LEN);

	//print out an integer map for XORing each bit
	int max_reduction_bit = 0;
	printf("The following XOR reduction map can be used to get the sequence ID of an address\n");
	printf("int xor_map[%d] = {%d", ADDR_BITS, xor_map[0]);
	for(uint64_t b = 1; b < ADDR_BITS; ++b)
	{
		printf(", %d", xor_map[b]);
		if(xor_map[b] > max_reduction_bit)
			max_reduction_bit = xor_map[b];
	}
	printf("};\n\n");

	//Setting bits in mask from returned xor_map
	int mask_bits = find_set_bit(max_reduction_bit);
	uint64_t *mask = calloc(mask_bits+1, sizeof(uint64_t));
	for(int i = 0; i <= mask_bits; i++)
	{
		mask[i] = 0ULL;
		for(uint64_t b = 0; b < ADDR_BITS; b++)
		{
			if(xor_map[b] != -1 && is_bit_k_set(xor_map[b], i))
				mask[i] |= (1ULL<<b);
		}
	}

	printf("The following mask can be used to get the sequence ID of an address\n");
	printf("uint64_t mask[%d] = {", mask_bits+1);
	for(int i = 0; i <= mask_bits; i++)
	{
		printf("0x%010lx", mask[i]);
		if(i < mask_bits)
			printf("ULL, ");
		else
			printf("ULL};\n\n");
	}

	//Print XOR bits for each address
	printf("Showing the XOR bits for each bit of the address to find the sequence ID\n");
	for(int i = 0; i <= mask_bits; ++i)
	{
		int start = 1;
		printf("ID%d = ", i);
		for(uint64_t b = START_BIT(SEQ_LEN); b < ADDR_BITS; ++b)
		{
			if(xor_map[b] != -1)
			{				
				if(start && is_bit_k_set(xor_map[b], i))
				{
					printf("A%02ld", b);
					start = 0;
				}
				else if(is_bit_k_set(xor_map[b], i))
				{
					printf("⊕ A%02ld", b);
				}
			}
		}
		putchar('\n');
	}
	putchar('\n');

	//print out a mask which can be used to generate the slice from a given address	
	printf("Showing ID mapping split into binary (MSB->LSB) (with the bits belonging to cache line index stripped)\n");
	for(int i = 0; i <= mask_bits; i++)
	{
		printf("ID%d = |", i);
		for(uint64_t b = ADDR_BITS-1; b != find_set_bit(L3_CACHELINE)-1; --b)
		{
			if(xor_map[b] != -1 && is_bit_k_set(xor_map[b], i))
				printf("▄");
			else
				printf(" ");
		}
		if(i < mask_bits)
			printf("|\n");
		else
			printf("|\n\n");
	}

	//If power of two, then we don't need to find the master sequence, as the XOR reduction is the only step required to get the mapping correctly.
	if(!is_power_of_two(num_cbos))
	{
		find_master_sequence(adj,mem, len, master_sequence, SEQ_LEN, xor_map);
		//print the master sequence
		printf("Master Sequence: \n");
		for(int i = 0; i < SEQ_LEN; ++i)
		{
			printf("%d", master_sequence[i]);
			if((i % 4) == 3)
				putchar(' ');
		}
		putchar('\n');
		putchar('\n');

		//print the master sequence
		printf("Master Sequence: \n");
		for(int i = 0; i < SEQ_LEN; ++i)
		{
			printf("%d", master_sequence[i]);
			if((i % 4) == 3)
				putchar(' ');
		}
		putchar('\n');
		putchar('\n');

		printf("int master_sequence[%d] = {", SEQ_LEN);
		for(int i = 0; i < SEQ_LEN; i++)
		{
			if(i < SEQ_LEN-1)
				printf("%d, ", master_sequence[i]);
			else
				printf("%d};\n\n", master_sequence[i]);
		}

		printf("Master Sequence (split into binary, to better visualise its effect on each bit of the XOR reduction/mask) \n");
		for (int i = 0; i <= find_set_bit(CORES-1); ++i)
		{
			printf("M%d = |", i);
			for(int s = 0; s < SEQ_LEN; ++s)
			{
				if(is_bit_k_set(master_sequence[s], i))
					printf("▄");
				else
					printf(" ");
			}			
			if(i < find_set_bit(CORES-1))
				printf("|\n");
			else
				printf("|\n\n");
		}
	}
	else
	{		
		printf("No master sequence, number of processor cores is a power of 2.\n\n");
	}


	printf("Testing first 128 cache lines starting from address 0x0:\n");
	for (int i = 0; i < 128; ++i)
	{
		int calc_slice = -1;
		if(is_power_of_two(num_cbos))
		{
			calc_slice = calculate_address_slice(0x0+(i*L3_CACHELINE), NULL, SEQ_LEN, xor_map);
		}
		else
		{
			calc_slice = calculate_address_slice(0x0+(i*L3_CACHELINE), master_sequence, SEQ_LEN, xor_map);
		}
		printf("%d", calc_slice);
		if(i%4==3)
			putchar(' ');
	}
	putchar('\n');
	putchar('\n');

	//Now, get a random address (of any offset, bit 6 can be set etc.) and get its offset into the sequence of its corresponding ID. This is now its slice number.
	printf("Testing found slice mapping function with some random values:\n");
	for(int i = 0; i < 32; ++i)
	{			
		//Get random offset into mem
		size_t rnd = (size_t) rand() % len;
		int calc_slice = -1;
		if(is_power_of_two(num_cbos))
		{
			calc_slice = calculate_address_slice(vtop(pid, (uint64_t)&mem[rnd]), NULL, SEQ_LEN, xor_map);
		}
		else
		{
			calc_slice = calculate_address_slice(vtop(pid, (uint64_t)&mem[rnd]), master_sequence, SEQ_LEN, xor_map);
		}

		printf("Phys Addr: 0x%09lx | Calc Slice: %d | Real Slice: %d\n", vtop(pid, (uint64_t)&mem[rnd]), calc_slice, get_slice_value(mem, len, rnd));
	}
	putchar('\n');
	putchar('\n');

	//Release (the dragon)
	munmap(mem, len * sizeof(uint8_t));
	adjacent_address_destroy(adj);
	free(mask);
	free(master_sequence);
	return ret;
}
