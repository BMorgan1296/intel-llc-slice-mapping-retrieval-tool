#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#ifndef HELPERS_H
#define HELPERS_H

void clflush(void *v, void *v1);
int find_set_bit(uint64_t n);
int does_val_differ_by_one(uint64_t a, uint64_t b);
int is_bit_k_set(uint64_t num, int k);
int is_power_of_two(uint64_t x);
void shuffle(void *array, size_t n, size_t size);

double calculate_mean(double data[], uint32_t len);
double calculate_stddev(double data[], uint32_t len, double mean);
double calculate_zscore(double datum, double mean, double stddev);

//From https://cs.adelaide.edu.au/~yval/Mastik/
static inline int memaccess(void *v) {
  int rv;
  asm volatile("mov (%1), %0": "+r" (rv): "r" (v):);
  return rv;
}

static inline uint32_t memaccesstime(void *v) {
  uint32_t rv;
  asm volatile (
      "mfence\n"
      "lfence\n"
      "rdtscp\n"
      "mov %%eax, %%esi\n"
      "mov (%1), %%eax\n"
      "rdtscp\n"
      "sub %%esi, %%eax\n"
      : "=&a" (rv): "r" (v): "ecx", "edx", "esi");
  return rv;
}

///////////////////////////////////////////////////////////////////////////////////////
// https://github.com/cgvwzq/evsets/blob/master/browser/virt_to_phys.c
// Thank you cgvwzq for this
uint64_t vtop(unsigned pid, uint64_t vaddr);
uint64_t ptos(uint64_t paddr, uint64_t bits);

#endif //HELPERS_H