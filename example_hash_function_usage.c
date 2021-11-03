//////////////////////////////////////////////////////////////////////////////////////////
// gcc -o example example_hash_function_usage.c
//////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

//////////////////////////////////////////////////////////////////////////////////////////

int get_msb(uint64_t n)
{
    if (n == 0)
        return 0;
 
    int msb = 0;
    n = n / 2;
    while (n != 0) {
        n = n / 2;
        msb++;
    }
 
    return (1 << msb);
}

int is_bit_k_set(uint64_t num, int k)
{
    if(num & (1ULL << (k)))
        return 1;
    else
        return 0;
}

uint64_t count_bits(uint64_t n)
{
    uint64_t count = 0;
    while (n)
    {
        n &= (n-1) ;
        count++;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////////////////////

uint64_t calculate_xor_reduction_with_map(uint64_t addr, int *xor_map, int map_bits)
{
    uint64_t res = 0;

    //Check if any bits are set past map_bits, if so then we cannot recover the slice properly
    if((addr >> map_bits) > 0)
        return -1;

    for (int b = 0; b < map_bits; ++b)
    {
        if(is_bit_k_set(addr, b))
            res ^= (uint64_t)xor_map[b];
    }

    return res;
}

uint64_t calculate_xor_reduction_with_masks(uint64_t addr, uint64_t *masks, int reduction_bits)
{
    uint64_t res = 0;

    for (int b = 0; b < reduction_bits; ++b)
    {
        res |= ((uint64_t)(count_bits(masks[b] & addr) % 2) << b);
    }

    return res;
}

//////////////////////////////////////////////////////////////////////////////////////////

struct slice
{
    uint64_t address;
    int known_slice;
}typedef slice_t;

//Slices from 4KB random pages in a 6700K
slice_t example_addrs_6700K[1024] = {
    {0x1a7087000,0},
    {0x15889f000,3},
    {0x18182b000,0},
    {0x15f3d5000,0},
    {0x153ce5000,0},
    {0x214970000,1},
    {0x1e9ab5000,2},
    {0x125ae5000,3},
    {0x20f0ee000,2},
    {0x20f2ed000,3},
    {0x21c187000,1},
    {0x15b4e2000,1},
    {0x10c36d000,2},
    {0x1e9394000,1},
    {0x1e8bc3000,2},
    {0x16a924000,1},
};

slice_t example_addrs_9850H[1024] = {
    {0x27db0d6b6,0},
    {0x2a2a8cf87,1},
    {0x340afc41b,3},
    {0x150e1c464,2},
    {0x153723bf5,0},
    {0x074169861,0},
    {0x2521956ab,5},
    {0x00fbbe41c,5},
    {0x262ba15e7,5},
    {0x3a0dd6690,1},
    {0x019fb5c5b,3},
    {0x2c47e5490,5},
    {0x00fe4be1e,2},
    {0x1f9bbb7e5,5},
    {0x2d7168302,2},
    {0x22bc5c7a8,4},
};

int main()
{
    //The following XOR reduction map can be used to get the slice index or sequence ID of an address
    int xor_map_6700K[35] = {0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 1, 2, 1, 2, 1, 2, 1, 3, 1, 2, 3, 2, 3, 2, 3, 1, 3, 1, 3, 2, 1, 2, 1, 3, 2};

    //The following mask can be used to get the slice index or sequence ID of an address
    uint64_t xor_masks_6700K[2] = {0x035f575440ULL, 0x06b5faa880ULL};

    printf("Example address -> slice for 4-core Intel Core i7-6700K using XOR-reduction map\n");
    printf("Address | Known Slice | Calculated Slice\n");
    for (int i = 0; i < 16; ++i)
    {
        printf("0x%lx | %d | %lu\n", example_addrs_6700K[i].address, 
                                     example_addrs_6700K[i].known_slice, 
                                     calculate_xor_reduction_with_map(example_addrs_6700K[i].address, xor_map_6700K, 35));
    }

    printf("\nExample address -> slice for 4-core Intel Core i7-6700K using XOR-reduction masks\n");
    printf("Address | Known Slice | Calculated Slice\n");
    for (int i = 0; i < 16; ++i)
    {
        printf("0x%lx | %d | %lu\n", example_addrs_6700K[i].address, 
                                     example_addrs_6700K[i].known_slice, 
                                     calculate_xor_reduction_with_masks(example_addrs_6700K[i].address, xor_masks_6700K, 2));
    }

    #define SEQ_LEN 128
    #define L3_CACHELINE 64
    #define L3_CACHELINE_BITS 6
    uint64_t xor_masks_9850H[7] = {0x01ae7be000ULL, 0x035cf7c000ULL, 0x0317946000ULL, 0x022f28c000ULL, 0x005e518000ULL, 0x00bca30000ULL, 0x00d73de000ULL};
    int master_sequence_9850H[SEQ_LEN] = {0,1,2,3,1,4,3,4,1,0,3,2,0,5,2,5,1,0,3,2,0,5,2,5,0,5,2,5,1,4,3,4,0,1,2,3,5,0,5,2,5,0,5,2,4,1,4,3,1,0,3,2,4,1,4,3,4,1,4,3,5,0,5,2,2,3,0,1,5,2,5,0,3,2,1,0,4,3,4,1,3,2,1,0,4,3,4,1,4,3,4,1,5,2,5,0,2,3,0,1,3,4,1,4,3,4,1,4,2,5,0,5,3,2,1,0,2,5,0,5,2,5,0,5,3,4,1,4};

    printf("\nExample address -> sequenceID -> slice for 6-core Intel Core i7-9850H using XOR-reduction masks and master sequence\n");
    printf("Address | Known Slice | Sequence ID | Calculated Slice\n");
    for (int i = 0; i < 16; ++i)
    {
        int seq_id = calculate_xor_reduction_with_masks(example_addrs_9850H[i].address, xor_masks_9850H, 7);
        int sequence_offset = ((uint64_t)example_addrs_9850H[i].address % (SEQ_LEN*L3_CACHELINE)) >> L3_CACHELINE_BITS;
        int calc_slice = master_sequence_9850H[sequence_offset ^ seq_id]; 

        printf("0x%09lx | %d | %03u | %d\n", example_addrs_9850H[i].address, 
                                          example_addrs_9850H[i].known_slice, 
                                          seq_id,
                                          calc_slice);
    }
}
