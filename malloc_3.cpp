#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>

#define MAX_ORDER 10
#define MIN_ORDER_SIZE 128
#define MAX_SIZE (128 * 1024)
#define NUM_INIT_BLOCKS 32

struct MallocMetadata
{
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *previos_meta_data;
};

struct MetaDataStats
{
    size_t allocated_blocks = 0;
    size_t allocated_bytes = 0;
    size_t meta_data_size = 0;
};

// MallocMetadata *first_meta_deta = nullptr;
MetaDataStats meta_data_stats;

MallocMetadata* arr[MAX_ORDER+1] = {};
int heap_size = 0;
bool inited = false;

// insert new free block, given the metadata
void insert(MallocMetadata* md){
    int index = (int)log2(md->size) - 7; // -7 because we start indexing from 0
    MallocMetadata* ptr = arr[index];
    if(ptr == nullptr){ //first block
        arr[index] = md;
        return;
    }
    if(md < ptr){ // should be the new head of the list
        ptr->previos_meta_data = md;
        md->next = ptr; 
        arr[index] = md;
        return;
    }
    while(ptr->next != nullptr && ptr->next < md){ // insert by accending address
        ptr = ptr->next;
    }
    md->previos_meta_data = ptr;
    if (ptr->next != nullptr){
        ptr->next->previos_meta_data = md;
        md->next = ptr->next;
    }
    ptr->next = md;

}


// remove the block from the array; assume its there
void remove_by_md(MallocMetadata* md){
    int index = (int)log2(md->size) - 7; // -7 because we start indexing from 0
    if(arr[index] == md){ // first
        arr[index] = md->next;
        if(md->next != nullptr){
            arr[index]->previos_meta_data = nullptr;
        }
        md->next = nullptr;
    }
    else{
        md->previos_meta_data->next = md->next;
        if(md->next != nullptr){
            md->next->previos_meta_data = md->previos_meta_data;
            md->next = nullptr;
        }
        md->previos_meta_data = nullptr;
    }
}

void init_malloc(){ // initialize the array of free blocks and allocated
    void* start_heap = sbrk(0); // Get current program break
    uint64_t align = (NUM_INIT_BLOCKS * MAX_SIZE) - (uint64_t)start_heap % (NUM_INIT_BLOCKS * MAX_SIZE);
    sbrk(align);
    start_heap = sbrk(NUM_INIT_BLOCKS * MAX_SIZE); // the actual memory heap we gonna allocate
    for(int i = 0; i < NUM_INIT_BLOCKS; i++){
        MallocMetadata* md = (MallocMetadata*)((char*)start_heap + (i * MAX_SIZE));
        md->size = MAX_SIZE;
        md->is_free = true;
        md->next = nullptr;
        md->previos_meta_data = nullptr; 
        insert(md);
    }
    heap_size = NUM_INIT_BLOCKS * MAX_SIZE;
    meta_data_stats.meta_data_size = sizeof(MallocMetadata);
}


MallocMetadata* find_buddy(MallocMetadata* md){
    return (MallocMetadata*)((uint64_t)md ^ md->size); // XOR for finding buddy
}


 // function to split a block for two seperate buddys blocks
void split(MallocMetadata* md, size_t dedireSize){
    if(dedireSize >= md->size / 2 || MIN_ORDER_SIZE == md->size){ // check next divition will be illegal => that the right split
        return;
    }
    md->size /= 2;
    // insert the new buddy
    MallocMetadata* buddyMd = (MallocMetadata*)((char*)md + md->size);
    buddyMd->size = md->size;
    buddyMd->is_free = true;
    buddyMd->next = nullptr;
    buddyMd->previos_meta_data = nullptr; 
    insert(buddyMd);

    split(md, dedireSize);

}


// help function for combine to check if can merge, for srealloc, make an iterative loop for fit the desire size
bool combinable(MallocMetadata* md, size_t size){
    if(md->size == MAX_SIZE){
        return true;
    }
    MallocMetadata* merged = md;
    size_t merged_size = md->size;
    while(merged_size < size){
        MallocMetadata* buddy = find_buddy(merged);
        if(buddy->is_free && merged_size == buddy->size){
            if(md < buddy){
                merged = md;
            }
            else{
                merged = buddy;
            }
            merged_size *= 2;
        }
        else{
            return false;
        }
    }
    return true;
}

// function to combine two free buddy blocks, iteratively, until no two buddy blocks are free
MallocMetadata* combine(MallocMetadata* md, size_t size = MAX_SIZE){ 
    if (md->size >= size){ // for srealloc
        return md;
    }
    MallocMetadata* buddy = find_buddy(md);
    if(buddy->is_free && md->size == buddy->size){
        MallocMetadata* mergedMd = nullptr;
        remove_by_md(md);
        remove_by_md(buddy);
        if(md < buddy){
            mergedMd = md;
        }
        else{
            mergedMd = buddy;
        }
        mergedMd->size *= 2;
        mergedMd->is_free = true;
        mergedMd->next = nullptr;
        mergedMd->previos_meta_data = nullptr;

        insert(mergedMd);

        return combine(mergedMd, size);
    }
    else{
        return md;
    }
}


// allocate block from the free blocks array and remove it from there
MallocMetadata* allocate_block(size_t size){
    int index = (int)(log2(size + sizeof(MallocMetadata) - 1) - 6);
    if(index < 0){ 
        index = 0;
    }
    while (index <= MAX_ORDER && arr[index] == nullptr){ // find the smallest order that available
        index++;
    }
    if (index == MAX_ORDER + 1){
        return nullptr;
    }

    MallocMetadata* meta_data = arr[index]; // first free block at the correct size

    arr[index] = arr[index]->next;
    if(arr[index] != nullptr){
        arr[index]->previos_meta_data = nullptr;
    }
    meta_data->next = nullptr; // first in line, prev is  already nullptr
    split(meta_data, size + sizeof(MallocMetadata));
    meta_data->is_free = false;
    return meta_data;
}

void* allocate_mmap(size_t size){
    void* ptr = mmap(NULL, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(ptr == MAP_FAILED){
        return nullptr; 
    }
    else{
        MallocMetadata* meta_data = (MallocMetadata*)ptr;
        meta_data->size = size + sizeof(MallocMetadata);
        meta_data_stats.allocated_blocks++;
        meta_data_stats.allocated_bytes += meta_data->size;
        heap_size += meta_data->size;
        return (void *)((char*)ptr + sizeof(MallocMetadata));
    }
}


//-------------------------------- malloc_2 init --------------------------------------/

void *smalloc(size_t size){
    if(!inited){
        init_malloc();
        inited = true;
    }
    if (size == 0 || size > 1e8){
        return nullptr;
    }
    MallocMetadata *meta_data;
    if(size > MAX_SIZE - sizeof(MallocMetadata)){ // IMPLEMENT VERY LARGE 
        return allocate_mmap(size);
    }
    else{ // regular size, allocate block in array
    	meta_data = allocate_block(size);
        if(meta_data == nullptr){
            return nullptr;
        }
        meta_data_stats.allocated_blocks++;
        meta_data_stats.allocated_bytes += meta_data->size;
        return (void *)((char*)meta_data + sizeof(MallocMetadata));
    }
    
}

void *scalloc(size_t num, size_t size)
{
    int res = num*size;
    void *ptr = smalloc(res);
    if (ptr == nullptr){
        return nullptr;
    }
    memset(ptr, 0, num*size);
    return ptr;
}

// the actual fucntion for free allocated blocks
void free_memory(void *ptr) {
    MallocMetadata *meta_data = (MallocMetadata *)((char*)ptr - sizeof(MallocMetadata)); // SHOULD BE: sizeof(MallocMetadata) ?
    if(meta_data->is_free){
        return;
    }
    if(meta_data->size > MAX_SIZE){ // is mmap()
        heap_size -= meta_data->size;
        meta_data_stats.allocated_blocks--;
        meta_data_stats.allocated_bytes -= meta_data->size; // SHOULD BE -sizeof(MallocMetadata) ?
        munmap(meta_data, meta_data->size);
    }
    else{ // in the array
        meta_data->is_free = true;
        insert(meta_data);
        meta_data_stats.allocated_blocks--;
        meta_data_stats.allocated_bytes -= meta_data->size;
        combine(meta_data);
    }
}

// wrapper for free blocks
void sfree(void *ptr)
{
    if (ptr == nullptr){ // nothing to free
        return;
    }
    free_memory(ptr);
}

void *srealloc(void *oldp, size_t size)
{
    if (oldp == nullptr) 
        return smalloc(size);

    MallocMetadata *meta_data = (MallocMetadata *)((char*)oldp - sizeof(MallocMetadata));
    size_t meta_size = meta_data->size;

    if (meta_size - sizeof(MallocMetadata) == size){
        return oldp;
    }

    if(meta_size > MAX_SIZE){         // for mmap
        void *newp = smalloc(size);
        // if (newp == nullptr)
        //     return nullptr;
        memmove(newp, oldp, meta_size);  //SHOLD BE MEMMOVE
        sfree(oldp);
        return newp;
    }

    if (meta_size - sizeof(MallocMetadata) > size){
        return oldp;
    }

    if(combinable(meta_data,size + sizeof(MallocMetadata))){ // we can realloc using merging
        meta_data->is_free = true;
        insert(meta_data);
        MallocMetadata* merged = combine(meta_data, size + sizeof(MallocMetadata));
        remove_by_md(merged); // we want to allocate it
        merged->is_free = false;
        memmove((void *)((char*)merged + sizeof(MallocMetadata)), oldp, meta_size); // maybe copy & free
        return (void *)((char*)merged + sizeof(MallocMetadata));
    }


    // for mmap or simply dont fixed by combinig
    void *newp = smalloc(size);
    if (newp == nullptr)
        return nullptr;
    memmove(newp, oldp, meta_size);  //SHOLD BE MEMMOVE
    sfree(oldp);
    return newp;

}

size_t _num_free_blocks() {
    // return meta_data_stats.free_blocks;
    int count = 0;
    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* head = arr[i];
        while (head != nullptr) {
            count++;
            head = head->next;
        }
    }
    return count;
}

size_t _num_free_bytes() {
    // return meta_data_stats.free_bytes;
    int count = 0;
    int block_size = 128;
    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* head = arr[i];
        while (head != nullptr) {
            count += block_size - sizeof(MallocMetadata);
            head = head->next;
        }
        block_size *= 2;
    }
    return count;
}

size_t _num_allocated_blocks() {
    // return meta_data_stats.allocated_blocks;
    return meta_data_stats.allocated_blocks + _num_free_blocks();
}

size_t _num_allocated_bytes() {
    // return meta_data_stats.allocated_bytes;
    return heap_size - _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _num_meta_data_bytes() {
    // return meta_data_stats.meta_data_bytes;
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _size_meta_data() {
    return meta_data_stats.meta_data_size;
}