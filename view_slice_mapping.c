#include "uncore_address_map.h"

int main(int argc, char const *argv[])
{
	int ret = 0;
	//Holds the max amount of address to slice mappings (32768 * NUM_SEQUENCES)
	int16_t *slice_map = malloc((NUM_SEQUENCES*MAX_ID) * sizeof(int16_t));
	size_t len = (size_t)NUM_SEQUENCES*PAGE_SIZE;
	uint8_t *mem = mmap(NULL, sizeof(uint8_t) * len, PROT_READ | PROT_WRITE | PROT_EXEC, MMAP_FLAGS, -1, 0);
	if((size_t)mem == -1)
	{
		perror("get_slice_mapping()");
		exit(1);
	}

	printf("Printing out some random address slice values\n");
	sequence_data_t *seq_data = malloc(NUM_SEQUENCES * sizeof(sequence_data_t));
	for (size_t i = 0; i < 1024; i = i + 64)
	{
		size_t offset = i;
		int slice = get_slice_value(mem, len, offset);
		// if(slice > 6)
		// {
		// 	while(1)
		// 		printf("%p | %02d\n", &mem[offset], get_slice_value(mem, len, offset));
		// }
		printf("%p | %02d\n", &mem[offset], slice);

	}
	putchar('\n');

	//Get slice values from the perf counter library
	printf("Getting slice mapping\n");
	size_t max_seq_len = 0;
	size_t power = 1;
	//Search through varying sequence lengths up to 32768 (MAX_ID). This is an arbitrary limit.
	for (size_t seq_len = 1; seq_len < MAX_ID; seq_len = seq_len << 1)
	{
		size_t n_addr = NUM_SEQUENCES*seq_len;
		get_slice_values(mem, len, n_addr, (NUM_SEQUENCES*(seq_len >> 1)), slice_map);

		//Fill the sequence data with info from the slice mapping
		fill_seq_data(seq_data, mem, slice_map, seq_len);

		//Finding max sequence length.
		//Get the max ID
		for (int s = 0; s < NUM_SEQUENCES; ++s)
		{
			//Ignore 0xBADBAD
			if(seq_data[s].xor_op != 0xBADBAD && seq_data[s].xor_op > max_seq_len)
			{
				max_seq_len = seq_data[s].xor_op;
			}
		}

		//Find the closest upper power of 2. This will be the sequence length.
		//E.g. 3=>4, 25=>32, 127=>128
		while(power < max_seq_len)
		    power*=2;

		printf("Tried Sequence Length: %ld | Max Sequence Length Found: %ld | Max ID: %ld\n\n", seq_len, max_seq_len, power);
		
		//Print each sequence
		//print_slice_values(seq_data, seq_len);
		if(seq_len > power)
		{
			break;
		}
	}
	
	//Print each sequence
	print_slice_values(seq_data, power);

	printf("Max Sequence Length: %lu\n\n", power);

	//Release (the dragon)
	munmap(mem, len * sizeof(uint8_t));
	free(slice_map);
	free(seq_data);
	return ret;
}