#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "setup_info.h"
#include "helpers.h"

#ifndef UNCORE_ADDRESS_MAP_H
#define UNCORE_ADDRESS_MAP_H

//Paging setup
#if defined(USEHUGEPAGE)
	#ifndef MAP_HUGETLB
		#define MAP_HUGETLB 0x40000 /* arch specific */
	#endif
	#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB)
	#define PAGE_BITS 21
#elif !defined(USEHUGEPAGE)
	#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)
	#define PAGE_BITS 12
#endif

#define PAGE_SIZE (1<<PAGE_BITS)

//Cache Info (L3 not needed)
#define L1_SETS (L1D/L1_ASSOCIATIVITY/L1_CACHELINE)
#define L1_STRIDE (L1_CACHELINE * L1_SETS)

#define L2_SETS (L2/L2_ASSOCIATIVITY/L2_CACHELINE)
#define L2_STRIDE (L2_CACHELINE * L2_SETS)

#define MAX_ID 32768

#ifndef START_BIT
	#define START_BIT(s) (find_set_bit(s*L3_CACHELINE))
#endif

struct sequence_data
{
	uint64_t vaddr;
	uint64_t paddr;
	int16_t sequence[MAX_ID];
	uint64_t xor_op;
} typedef sequence_data_t;

struct adjacent_address
{
	uint64_t bit_n_a[ADDR_BITS][NUM_ADJ_ADDR];
	uint64_t bit_n_b[ADDR_BITS][NUM_ADJ_ADDR];
	int count[ADDR_BITS];
	int16_t slice_map_a[ADDR_BITS][NUM_ADJ_ADDR][PAGE_SIZE/L3_CACHELINE];
	int16_t slice_map_b[ADDR_BITS][NUM_ADJ_ADDR][PAGE_SIZE/L3_CACHELINE];
	//Sequence data for the above addresses
	sequence_data_t seq_a[ADDR_BITS][NUM_ADJ_ADDR];
	sequence_data_t seq_b[ADDR_BITS][NUM_ADJ_ADDR];
} typedef adj_addr_t;

//////////////////////////////////////////////////////////////////////////////////////

adj_addr_t *adjacent_address_init();
void adjacent_address_destroy(adj_addr_t *adj);

//////////////////////////////////////////////////////////////////////////////////////

double get_slice_access_time(uint8_t *mem, uint64_t len, uint64_t offset);
int access_get_slice(uint8_t *mem, uint64_t len, uint64_t offset);

//////////////////////////////////////////////////////////////////////////////////////

int get_slice_value(uint8_t *mem, uint64_t len, uint64_t offset);
void get_slice_values(uint8_t *mem, uint64_t len, uint64_t n_addr, uint64_t start_offset, int16_t *slice_map);
void fill_seq_data(sequence_data_t *seq_data, uint8_t *mem, int16_t *slice_map, uint64_t seq_len);
void print_slice_values(sequence_data_t *seq_data, uint64_t seq_len);


void adjacent_address_search(adj_addr_t *adj, uint8_t *mem, uint64_t len, uint64_t seq_len);
int get_slice_values_adj(adj_addr_t *adj, uint8_t *mem, uint64_t len, uint64_t seq_len);
void fill_seq_data_adj(adj_addr_t *adj, uint8_t *mem, uint64_t seq_len);
void print_slice_values_adj(adj_addr_t *adj, uint8_t *mem, uint64_t seq_len);

void find_xor_for_each_bit(adj_addr_t *adj, int xor_map[ADDR_BITS], uint64_t seq_len);
uint64_t calculate_xor_reduction(uint64_t addr, int xor_map[ADDR_BITS]);
void find_master_sequence(adj_addr_t *adj, uint8_t *mem, uint64_t len, int16_t *master_sequence, uint64_t seq_len, int xor_map[ADDR_BITS]);

int calculate_address_slice(uint64_t paddr, int16_t *master_sequence, uint64_t seq_len, int xor_map[ADDR_BITS]);
//////////////////////////////////////////////////////////////////////////////////////

#endif //UNCORE_ADDRESS_MAP_H