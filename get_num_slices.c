#include "perf_counters.h"

#include <stdio.h>

int main()
{
	printf("%u\n", uncore_get_num_cbo(0));
	return 0;
}
