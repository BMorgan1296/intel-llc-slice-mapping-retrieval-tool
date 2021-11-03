# Intel LLC Slice Mapping Retrieval Tool

## Introduction

We describe a tool developed for automatically retrieving the last-level cache slice hashing function used in Intel processors. This function is not publicly disclosed nor described by Intel, negatively impacting the development of processor cache side-channel research. 

To develop this tool, we take several liberties with the systems it is run on, mainly by requiring high privilege levels to expose low level processor performance counter interfaces using our `perfcounters` library. 

As a brief overview, our tool works by finding pairs of physical addresses which differ on one bit, which we define *adjacent addresses*. These addresses tell us information about the slice mapping for their differing bit *k*. Thus, by finding pairs of these adjacent addresses, the slice mapping can be recovered for every addressable bit of memory. 

This is followed by further intricacies which depend on whether the function is linear (2^n slices), or non-linear (non-2^n slices).

## Requirements

This tool requires installation of our custom performance counter interface, `perfcounters` available [here](https://git.sec.cs.adelaide.edu.au/).

## Usage

`sudo ./slice_mapping.sh --[view|get] [--save]`

To run the tool, use the `slice_mapping.sh` script to either:
* `--view` to see the slice mapping for a contiguous portion of memory.
* `--get` to retrieve the slice mapping.
  * `--save` to optionally save this to file in the `./output` directory with timestamp.

## How Do I Use This?
See `example_hash_function_usage.c` to observe code samples utilising the returned information from this tool, calculating arbitrary address slice values.

We show how to use the two main formats provided to calculate the XOR-reduction using either an xor map or group of masks. Following this is code to determine the slice index of addresses on a 6-core machine, utilising the XOR-reduction stage as well as master sequence.

## To Do
* 12th Generation Alder Lake processors.
* Xeon processors (requires modification to `perfcounters` interface).
* Generalise this across all processors by reconciling linear and non-linear hash functions.
