#include <stdio.h>
#include <stdint.h>

#define LAST(k,n) ((k) & ((1<<(n))-1))
#define MID(k,m,n) LAST((k)>>(m),((n)-(m)))

int find_first_set_bit(uint64_t nth)
{
	int pos = 0;
	int kth = 0;
	while(kth == 0)
	{
		kth = MID(nth, pos, pos+1);
		pos += 1;
	}
	return pos-1;
}

void test_xor_compare(uint64_t *mask, int len)
{
	for (int i = 0; i < len; ++i)
	{
		for (int j = i+1; j < len; ++j)
		{
			uint64_t mask1 = mask[i] >> find_first_set_bit(mask[i]);
			uint64_t mask2 = mask[j] >> find_first_set_bit(mask[j]);
			printf("%d %d | 0x%05lx ^ 0x%05lx =\t0x%05lx\n", i, j, mask1, mask2, mask1 ^ mask2);
		}
		printf("\n");
	}
}

int main(int argc, char const *argv[])
{
	uint64_t linear_old[4] = {0x1b5f575440ULL, 0x2eb5faa880ULL, 0x3cccc93100ULL, 0x31aeeb1000ULL}; 
	uint64_t linear_new[3] = {0x1b5f575440ULL, 0x01aeeb1200ULL, 0x06d87f2c00ULL};
	uint64_t non_linear[7] = { 0x01ae7be000ULL, 0x035cf7c000ULL, 0x0717946000ULL, 0x062f28c000ULL, 0x045e518000ULL, 0x00bca30000ULL, 0x00d73de000ULL};

	test_xor_compare(non_linear, 7);

	test_xor_compare(linear_old, 4);

	test_xor_compare(linear_new, 3);

	return 0;
}

// ID0 = (35cf7c << 0xB
// ID1 = (35cf7c << 0xC
// ID2 = (35cf7c ^ (35cf7c >> 0x2)) << D
// ID3 = (35cf7c ^ (35cf7c >> 0x2)) << E
// ID4 = (35cf7c ^ (35cf7c >> 0x2)) << F
// ID5 = (35cf7c ^ (35cf7c >> 0x2)) << 0x10
// ID6 = (35cf7c << 0xA) //bit 12 gets absorbed when using base sequence and XOR reduction in my tool