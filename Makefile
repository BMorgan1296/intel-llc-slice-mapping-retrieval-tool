CC=gcc
CFLAGS= -O2 $(OPS) -I/usr/local/include -g -fsanitize=address 
.DEFAULT_GOAL := all
BUILD_DIR = $(shell pwd)
LDFLAGS +=  -lm -lpthread -lperf_counters

helpers.o: helpers.c setup_info.h
	$(CC) $(CFLAGS) -c $< $(LDFLAGS)

uncore_address_map.o: uncore_address_map.c
	$(CC) $(CFLAGS) -c $^ $(LDFLAGS)

adjacent_address_search.o: adjacent_address_search.c
	$(CC) $(CFLAGS) -c $^ $(LDFLAGS)

view_slice_mapping: view_slice_mapping.c uncore_address_map.o helpers.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

get_slice_mapping: get_slice_mapping.c adjacent_address_search.o uncore_address_map.o helpers.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

get_num_slices: get_num_slices.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

all: view_slice_mapping get_slice_mapping get_num_slices

clean:
	rm -rf view_slice_mapping get_slice_mapping get_num_slices *.a *.o
