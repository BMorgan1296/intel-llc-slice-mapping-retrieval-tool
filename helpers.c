#include "helpers.h"

void clflush(void *v, void *v1) 
{
	asm volatile ("clflush 0(%0)\n": : "r" (v):);
}

int find_set_bit(uint64_t n)
{
	int pos = 0;
	n >>= 1;
	while(n != 0)
	{
		pos += 1;
		n >>= 1;
	}
	return pos;
}

int does_val_differ_by_one(uint64_t a, uint64_t b)
{
	uint64_t c = (a) ^ (b);

	if(c == 0)
		return 0;
	else if((c & (c-1)) == 0)
		return find_set_bit(c);
	else
		return 0;
}

int is_bit_k_set(uint64_t num, int k)
{
	if(num & (1ULL << (k)))
		return 1;
	else
		return 0;
}

//https://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2#600306
int is_power_of_two(uint64_t x)
{
	return (x != 0) && ((x & (x - 1)) == 0);
}

//https://stackoverflow.com/questions/6127503/shuffle-array-in-c
void shuffle(void *array, size_t n, size_t size) 
{
    char tmp[size];
    char *arr = array;
    size_t stride = size * sizeof(char);

    if (n > 1) {
        size_t i;
        for (i = 0; i < n - 1; ++i) {
            size_t rnd = (size_t) rand();
            size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

            memcpy(tmp, arr + j * stride, size);
            memcpy(arr + j * stride, arr + i * stride, size);
            memcpy(arr + i * stride, tmp, size);
        }
    }
}

double calculate_mean(double data[], uint32_t len)
{	
	double sum = 0.0;
	for (int i = 0; i < len; ++i) {
	    sum += data[i];
	}
	return (sum / (double)len);
}

double calculate_stddev(double data[], uint32_t len, double mean)
{
    double stddev = 0.0;
    for (int i = 0; i < len; ++i)
    {
        stddev += pow(data[i] - mean, 2);
    }
    return sqrt(stddev / len);
}

double calculate_zscore(double datum, double mean, double stddev)
{
	return ((datum - mean) / stddev);
}

///////////////////////////////////////////////////////////////////////////////////////
// https://github.com/cgvwzq/evsets/blob/master/browser/virt_to_phys.c
// Thank you cgvwzq for this
unsigned int count_bits(uint64_t n)
{
	unsigned int count = 0;
	while (n)
	{
		n &= (n-1) ;
		count++;
	}
	return count;
}

uint64_t vtop(unsigned pid, uint64_t vaddr)
{	
	//Bring the beginning of the sequence and the requested address into memory
	memaccess((void *)vaddr);
	
	char path[1024];
	sprintf (path, "/proc/%u/pagemap", pid);
	int fd = open (path, O_RDONLY);
	if (fd < 0)
	{
		return -1;
	}

	uint64_t paddr = -1;
	uint64_t index = (vaddr / 4096) * sizeof(paddr);
	if (pread (fd, &paddr, sizeof(paddr), index) != sizeof(paddr))
	{
		return -1;
	}
	close (fd);
	paddr &= 0x7fffffffffffff;
	return (paddr << 12) | (vaddr & (4096-1));
}

uint64_t ptos(uint64_t paddr, uint64_t bits)
{
	uint64_t ret = 0;
	uint64_t mask[3] = {0x1b5f575440ULL, 0x2eb5faa880ULL, 0x3cccc93100ULL}; // according to Maurice et al.
	switch (bits)
	{
		case 3:
			ret = (ret << 1) | (uint64_t)(count_bits(mask[2] & paddr) % 2);
		case 2:
			ret = (ret << 1) | (uint64_t)(count_bits(mask[1] & paddr) % 2);
		case 1:
			ret = (ret << 1) | (uint64_t)(count_bits(mask[0] & paddr) % 2);
		default:
		break;
	}
	return ret;
}