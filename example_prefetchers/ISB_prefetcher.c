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

#define MAX_COUNTER_VALUE 3 //Maximum value of the confidence counter
#define CHUNK_SIZE 256 // Maximum number of addresses in a temporal stream
#define DEGREE 2 //Number of prefetches to generate
#define TRAINING_UNIT_ENTRIES 128 //training_unit_entries
#define PREFETCH_CANDIDATES_PER_ENTRY 16 //Number of prefetch candidates stored in a SP-AMC entry
#define ADDRESS_MAP_CACHE_ENTRIES 128 //Number of entries of the PS/SP AMCs


typedef unsigned long long int Addr_t;
typedef unsigned long long int uint64_t;

// Training Unit Entry holds the last accessed address
typedef struct TrainingUnitEntry
{
    Addr_t tag;
    Addr_t lastAddress;
    char clock_tag; //using clock replacement
}TrainingUnitEntry_t;

//
typedef struct TrainingUnit {
    TrainingUnitEntry_t TrainingUnitEntries[TRAINING_UNIT_ENTRIES];
    int clock_pointer; //using clock replacement
}TrainingUnit_t;


// Address Mapping entry, holds an address and a confidence counter 
typedef struct AddressMapping {
    Addr_t address;
    unsigned counter;
}AddressMapping_t;

/*
    Maps a set of contiguous addresses to another set of (not necessarily
    contigous) addresses, with their corresponding confidence counters
*/
typedef struct AddressMappingEntry
{
    Addr_t tag;
    AddressMapping_t mappings[PREFETCH_CANDIDATES_PER_ENTRY];
    char clock_tag;
}AddressMappingEntry_t;

typedef struct AddressMappingCache
{
    AddressMappingEntry_t AddressMappingEntries[ADDRESS_MAP_CACHE_ENTRIES];
    int clock_pointer;
}AddressMappingCache_t;

//training unit
TrainingUnit_t trainingUnit;
//physical to structural mapping cache
AddressMappingCache_t psAddressMappingCache;
//structural to physical mapping cache
AddressMappingCache_t spAddressMappingCache;

uint64_t structuralAddressCounter;


TrainingUnitEntry_t* findTrainingEntry(Addr_t pc)
{
    int i;
    for (i = 0;i < TRAINING_UNIT_ENTRIES;i++)
    {
        if(trainingUnit.TrainingUnitEntries[i].tag == pc)
        {
            //cache hit, set the clock tag to 1
            trainingUnit.TrainingUnitEntries[i].clock_tag = 1;
            return &trainingUnit.TrainingUnitEntries[i];
        }

    }
    //cache miss
    return NULL;
}

void insertTrainingEntry(Addr_t pc,Addr_t lastAddress)
{
    int clock_pointer = trainingUnit.clock_pointer;
    int find = -1;
    while (1)
    {
        if(trainingUnit.TrainingUnitEntries[clock_pointer].clock_tag==0)
        {
            // trainingUnit.TrainingUnitEntries[clock_pointer].valid = 1;
            trainingUnit.TrainingUnitEntries[clock_pointer].tag = pc;
            trainingUnit.TrainingUnitEntries[clock_pointer].lastAddress = lastAddress;
            trainingUnit.TrainingUnitEntries[clock_pointer].clock_tag = 1;
            find = clock_pointer;
        }
        else
        {
            trainingUnit.TrainingUnitEntries[clock_pointer].clock_tag = 0;
        }
        
        clock_pointer ++;
        if(clock_pointer>=TRAINING_UNIT_ENTRIES)
            clock_pointer = 0;
        if(find >= 0)
        {
            trainingUnit.clock_pointer = clock_pointer;
            break;
        }
    }
}

AddressMappingEntry_t* findAddressMappingEntry(AddressMappingCache_t* cache, Addr_t amc_address)
{
    int i;
    for(i= 0; i<ADDRESS_MAP_CACHE_ENTRIES;i++)
    {
        if(cache->AddressMappingEntries[i].tag == amc_address)
        {
            //cache hit, set the clock_tag to 1
            cache->AddressMappingEntries[i].clock_tag = 1;
            return &cache->AddressMappingEntries[i];
        }

    } 
    //cache miss
    return NULL;
}


int insertAddressMappingEntry(AddressMappingCache_t* cache, Addr_t amc_address)
{
    int clock_pointer = cache->clock_pointer;
    int find = -1;
    while (1)
    {
        if(cache->AddressMappingEntries[clock_pointer].clock_tag==0)
        {
            // trainingUnit.TrainingUnitEntries[clock_pointer].valid = 1;
            cache->AddressMappingEntries[clock_pointer].tag = amc_address;
            cache->AddressMappingEntries[clock_pointer].clock_tag = 1;
            find = clock_pointer;
        }
        else
        {
            cache->AddressMappingEntries[clock_pointer].clock_tag = 0;
        }
        
        clock_pointer ++;
        if(clock_pointer>=ADDRESS_MAP_CACHE_ENTRIES)
            clock_pointer = 0;
        if(find>=0)
        {   
            cache->clock_pointer = clock_pointer;
            return find;
        }
  
    }
}

AddressMapping_t* getPSMapping(Addr_t paddr)
{
    Addr_t amc_address = paddr / PREFETCH_CANDIDATES_PER_ENTRY;
    Addr_t map_index   = paddr % PREFETCH_CANDIDATES_PER_ENTRY;
    AddressMappingEntry_t* ps_entry = findAddressMappingEntry(&psAddressMappingCache,amc_address);
    int entryIndex;
    if(!ps_entry)
    {
        entryIndex = insertAddressMappingEntry(&psAddressMappingCache,amc_address);
    }
    return &psAddressMappingCache.AddressMappingEntries[entryIndex].mappings[map_index];
}


void addStructuralToPhysicalEntry(Addr_t structral_address, Addr_t physical_address)
{
    Addr_t amc_address = structral_address / PREFETCH_CANDIDATES_PER_ENTRY;
    Addr_t map_index   = structral_address % PREFETCH_CANDIDATES_PER_ENTRY;
    AddressMappingEntry_t * sp_entry = findAddressMappingEntry(&spAddressMappingCache,amc_address);
    if(!sp_entry)
    {
        int sp_entry_index;
        sp_entry_index =  insertAddressMappingEntry(&spAddressMappingCache,amc_address);
        sp_entry = &spAddressMappingCache.AddressMappingEntries[sp_entry_index];
    }
    AddressMapping_t * mapping = &sp_entry->mappings[map_index];
    mapping->address = physical_address;
    mapping->counter = 1;
}   


void l2_prefetcher_initialize(int cpu_num)
{
    printf("ISB Prefetcher\n");
    // you can inspect these knob values from your code to see which configuration you're runnig in
    printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);
    
    structuralAddressCounter = 0;
    //initilize training unit
    int i,j;
    for(i = 0;i<TRAINING_UNIT_ENTRIES;i++)
    {
        trainingUnit.TrainingUnitEntries[i].tag = -1;
        trainingUnit.TrainingUnitEntries[i].clock_tag = 0;
    }
    trainingUnit.clock_pointer = 0;
    //initilize address mapping caches
    for(i = 0;i<ADDRESS_MAP_CACHE_ENTRIES;i++)
    {
        psAddressMappingCache.AddressMappingEntries[i].tag = -1;
        psAddressMappingCache.AddressMappingEntries[i].clock_tag = 0;
        for(j = 0;j<PREFETCH_CANDIDATES_PER_ENTRY;j++)
        {
            psAddressMappingCache.AddressMappingEntries[i].mappings[j].address = 0;
            psAddressMappingCache.AddressMappingEntries[i].mappings[j].counter = 0;
        }
        
        spAddressMappingCache.AddressMappingEntries[i].tag = -1;
        spAddressMappingCache.AddressMappingEntries[i].clock_tag = 0;

        for(j = 0;j<PREFETCH_CANDIDATES_PER_ENTRY;j++)
        {
            spAddressMappingCache.AddressMappingEntries[i].mappings[j].address = 0;
            spAddressMappingCache.AddressMappingEntries[i].mappings[j].counter = 0;
        }
    }
    psAddressMappingCache.clock_pointer = 0;
    spAddressMappingCache.clock_pointer = 0;
    printf("ISB Prefetcher initialized\n");
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
    // uncomment this line to see all the information available to make prefetch decisions
    //printf("(%lld 0x%llx 0x%llx %d %d %d)\n ", get_current_cycle(0), addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));

    Addr_t block_address = addr>>6; //cache line address
    Addr_t pc = ip;

    // Training, if the entry exists, then we found a correlation between
    // the entry lastAddress (named as correlated_addr_A) and the address of
    // the current access (named as correlated_addr_B)
    TrainingUnitEntry_t * entry = findTrainingEntry(pc);

    char correlated_addr_found = 0;
    Addr_t correlated_addr_A = 0;
    Addr_t correlated_addr_B = 0;
    if(entry)
    {
        correlated_addr_found = 1;
        correlated_addr_A = entry->lastAddress;
        correlated_addr_B = block_address;
    }
    else
    {
        insertTrainingEntry(pc,block_address);
    }
    
    if(correlated_addr_found)
    {
        
        AddressMapping_t *mapping_A = getPSMapping(correlated_addr_A);
        AddressMapping_t *mapping_B = getPSMapping(correlated_addr_B);
        if(mapping_A->counter > 0 && mapping_B->counter > 0)
        {
            //Entry for A and B
            if(mapping_B->address == (mapping_A->address + 1))
            {
                if(mapping_B->counter < MAX_COUNTER_VALUE)
                {
                    mapping_B->counter += 1;
                }
            }
            else
            {
                if(mapping_B->counter == 1)
                {
                    //reassign address
                    mapping_B->address = mapping_A->address + 1;
                    addStructuralToPhysicalEntry(mapping_B->address,correlated_addr_B);
                }
                else
                {
                    mapping_B->counter -= 1;
                }
            }
        }
        else
        {
            if(mapping_A->counter == 0)
            {
                // if A is not valid, generate a new structural address
                mapping_A->counter = 1;
                mapping_A->address = structuralAddressCounter;
                structuralAddressCounter += CHUNK_SIZE;
                addStructuralToPhysicalEntry(mapping_A->address, correlated_addr_A);
            }
            mapping_B->counter = 1;
            mapping_B->address = mapping_A->address + 1;
            //update SP-AMC
            addStructuralToPhysicalEntry(mapping_B->address, correlated_addr_B);
        }
    }

    Addr_t amc_address = block_address / PREFETCH_CANDIDATES_PER_ENTRY;
    Addr_t map_index   = block_address % PREFETCH_CANDIDATES_PER_ENTRY;
    AddressMappingEntry_t *ps_am = findAddressMappingEntry(&psAddressMappingCache,amc_address);
    if (ps_am) 
    {
        AddressMapping_t *mapping = &ps_am->mappings[map_index];
        if (mapping->counter > 0) {
            Addr_t sp_address = mapping->address / PREFETCH_CANDIDATES_PER_ENTRY;
            Addr_t sp_index   = mapping->address % PREFETCH_CANDIDATES_PER_ENTRY;
            AddressMappingEntry_t *sp_am =
                findAddressMappingEntry(&spAddressMappingCache, sp_address);
            if (!sp_am) {
                // The entry has been evicted, can not generate prefetches
                return;
            }
            unsigned int d;
            for (d = 1; d <= DEGREE && (sp_index + d) < PREFETCH_CANDIDATES_PER_ENTRY; d += 1)
            {
                AddressMapping_t *spm = &sp_am->mappings[sp_index + d];
                //generate prefetch
                if (spm->counter > 0) {
                    Addr_t pf_addr = spm->address << 6;
                    // addresses.push_back(AddrPriority(pf_addr, 0));
                    // check MSHR occupancy to decide whether to prefetch into the L2 or LLC
                    if(get_l2_mshr_occupancy(0) > 8)
                    {
                        // conservatively prefetch into the LLC, because MSHRs are scarce
                        l2_prefetch_line(0, addr, pf_addr, FILL_LLC);
                    }
                    else
                    {
                        // MSHRs not too busy, so prefetch into L2
                        l2_prefetch_line(0, addr, pf_addr, FILL_L2);
                    }
                }
            }
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
