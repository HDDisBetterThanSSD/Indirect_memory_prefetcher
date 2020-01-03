//
// Data Prefetching Championship Simulator 2
// Seth Pugsley, seth.h.pugsley@intel.com
//

/*
  
  This file describes a streaming prefetcher. Prefetches are issued after
  a spatial locality is detected, and a stream direction can be determined.

  Prefetches are issued into the L2 or LLC depending on L2 MSHR occupancy.

 */

#include <stdio.h>
#include "../inc/prefetcher.h"

#define PT_TABLE_SIZE 16
#define MAX_PREFETECH_DISTANCE 16
#define IPD_TABLE_SIZE 4
#define BASEADDR_ARRAY_LENGTH 4

int Shift_Values[4] = {2,3,4,-3};

enum All_states{initial,transient,steady,no_prediction};

typedef struct indirect_pattern_detector
{
    unsigned long long idx1;
    unsigned long long idx2;
    unsigned long long baseaddr_array[4][4];

}indirect_pattern_detector_t;


typedef struct prefetch_table
{
    /*streaming memory prefetcher*/
    int stride;
    // previous address
    unsigned long long prev_addr;
    // program counter
    unsigned int pc;
    // 4 states
    char state;

    // unsigned long long int addr;

    /*indirect memory prefetcher*/
    char imp_enable;
    int imp_index;
    unsigned long long int imp_base_addr;
    char imp_shift;
    int imp_hit_cnt;

}prefetch_table_t;


indirect_pattern_detector_t IPD[IPD_TABLE_SIZE];
prefetch_table_t PT[PT_TABLE_SIZE];

int replacement_index;

void l2_prefetcher_initialize(int cpu_num)
{
  printf("Streaming Prefetcher\n");
  // you can inspect these knob values from your code to see which configuration you're runnig in
  printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);

  int i;
  for(i=0; i<PT_TABLE_SIZE; i++)
  {
    PT[i].stride = 0;
    PT[i].state = 0;
    PT[i].pc = 0;
    PT[i].imp_enable = 0;
    PT[i].imp_index = -1;
    PT[i].imp_base_addr = 0;
    PT[i].imp_shift = 0;
    PT[i].imp_hit_cnt = 0;
  }

  replacement_index = 0;
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
  // uncomment this line to see all the information available to make prefetch decisions
  //printf("(%lld 0x%llx 0x%llx %d %d %d) ", get_current_cycle(0), addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));

    unsigned long long int cl_address = addr>>6; //cache line address

    // check for a detector hit
    int detector_index = -1;

    int i;
    for(i=0; i<PT_TABLE_SIZE; i++)
    {
        //table hit
        if(PT[i].pc == ip)
        {
            detector_index = i;
            int current_stride = addr-PT[i].prev_addr;
            switch (PT[i].state)
            {
            case initial:
                if(PT[i].stride != current_stride) //incorrect
                {
                    PT[i].state = transient;
                    PT[i].stride = current_stride;
                }
                else //correct
                {
                    PT[i].state = steady;
                }
                break;
            case transient:
                if(current_stride == PT[i].stride) // correct
                {
                    PT[i].state = steady;
                }
                else //incorrect
                {
                    PT[i].stride = current_stride;
                    PT[i].state = no_prediction;
                }
                break;
            case no_prediction:
                if(current_stride == PT[i].stride) //correct
                {
                    PT[i].state = transient;
                }
                else //incorrect
                {
                    PT[i].stride = current_stride;
                }
                break;
            case steady:
                if(current_stride != PT[i].stride)//in correct
                {
                    PT[i].state = initial;
                }
                break;
            default:
                break;
            }
            break;
            PT[i].prev_addr = addr;
        }
    }
    //table miss
    if(detector_index == -1)
    {
        // this is a new page that doesn't have a detector yet, so allocate one
        detector_index = replacement_index;
        replacement_index++;
        if(replacement_index >= PT_TABLE_SIZE)
        {
            replacement_index = 0;
        }
        // reset the oldest page
        PT[detector_index].pc = ip;
        PT[detector_index].stride = 0;
        PT[detector_index].state = initial;
        PT[detector_index].prev_addr = addr;
    }

    // prefetch if confidence is high enough
    if(PT[detector_index].state ==steady)
    {

        //PT[detector_index].pf_index = ;
        // perform prefetches
        unsigned long long int pf_address = PT[detector_index].stride + addr;
        
        // check MSHR occupancy to decide whether to prefetch into the L2 or LLC
        if(get_l2_mshr_occupancy(0) > 8)
        {
            // conservatively prefetch into the LLC, because MSHRs are scarce
            l2_prefetch_line(0, addr, pf_address, FILL_LLC);
        }
        else
        {
            // MSHRs not too busy, so prefetch into L2
            l2_prefetch_line(0, addr, pf_address, FILL_L2);
        }
    }
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{
  // uncomment this line to see the information available to you when there is a cache fill event
  //printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
}

void l2_prefetcher_heartbeat_stats(int cpu_num)
{
  printf("Prefetcher heartbeat stats\n");
}

void l2_prefetcher_warmup_stats(int cpu_num)
{
  printf("Prefetcher warmup complete stats\n\n");
}

void l2_prefetcher_final_stats(int cpu_num)
{
  printf("Prefetcher final stats\n");
}
