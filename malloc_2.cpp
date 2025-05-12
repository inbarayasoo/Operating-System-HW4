#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define MAX_SIZE 100000000

struct MallocMetadata
{
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *previos_meta_data;
};

struct MetaDataStats
{
    size_t free_blocks = 0;
    size_t free_bytes = 0;
    size_t allocated_blocks = 0;
    size_t allocated_bytes = 0;
    size_t meta_data_bytes = 0;
    size_t meta_data_size = 0;
};

MallocMetadata *first_meta_deta = nullptr;
MetaDataStats meta_data_stats;

size_t _num_free_blocks() {
    return meta_data_stats.free_blocks;
}

size_t _num_free_bytes() {
    return meta_data_stats.free_bytes;
}

size_t _num_allocated_blocks() {
    return meta_data_stats.allocated_blocks;
}

size_t _num_allocated_bytes() {
    return meta_data_stats.allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return meta_data_stats.meta_data_bytes;
}

size_t _size_meta_data() {
    return meta_data_stats.meta_data_size;
}

void *smalloc(size_t size)
{
    if (size > MAX_SIZE || size == 0)
        return nullptr;

    //allocating first block
    if (first_meta_deta == nullptr)
    {
        MallocMetadata *meta_data = (MallocMetadata *)sbrk(size + sizeof(MallocMetadata));
        if (meta_data == nullptr)
            return nullptr;

        meta_data->size = size;
        meta_data->is_free = false;
        meta_data->next = nullptr;
        meta_data->previos_meta_data = nullptr;
        first_meta_deta = meta_data;
        meta_data_stats.allocated_blocks++;
        meta_data_stats.allocated_bytes += size;
        meta_data_stats.meta_data_bytes += sizeof(MallocMetadata);
        meta_data_stats.meta_data_size = sizeof(MallocMetadata);
        return (void *)(meta_data + 1);
    }

    //searching free block 
    MallocMetadata *current_meta_data = first_meta_deta;
    MallocMetadata *previos_meta_data = nullptr;
    while (current_meta_data != nullptr)
    {
        if (current_meta_data->size >= size && current_meta_data->is_free) {
            current_meta_data->is_free = false;
            meta_data_stats.free_blocks--;
            meta_data_stats.free_bytes -= current_meta_data->size;
            return (void *)(current_meta_data + 1);
        }
        previos_meta_data = current_meta_data;
        current_meta_data = current_meta_data->next;
    }

    //allocate new block not found relative free block
    MallocMetadata *meta_data = (MallocMetadata *)sbrk(size + sizeof(MallocMetadata));
    if (meta_data == nullptr) 
        return nullptr;

    meta_data->size = size;
    meta_data->is_free = false;
    meta_data->next = nullptr;
    meta_data->previos_meta_data = previos_meta_data;
    if (previos_meta_data != nullptr) 
        previos_meta_data->next = meta_data;
    
    meta_data_stats.allocated_blocks++;
    meta_data_stats.allocated_bytes += size;
    meta_data_stats.meta_data_bytes += sizeof(MallocMetadata);
    meta_data_stats.meta_data_size = sizeof(MallocMetadata);
    return (void *)(meta_data + 1);
}

void *scalloc(size_t num, size_t size)
{
    int res = num*size;
    void *ptr = smalloc(res);
    if (ptr == nullptr) 
        return nullptr;

    memset(ptr, 0, res);
    return ptr;
}

void free_memory(void *ptr) {
    MallocMetadata *meta_data = (MallocMetadata *)ptr - 1;
    meta_data->is_free = true;
    meta_data_stats.free_blocks++;
    meta_data_stats.free_bytes += meta_data->size;
}

void sfree(void *ptr)
{
    if (ptr == nullptr)
        return;

    free_memory(ptr);
}

void *srealloc(void *oldp, size_t size)
{
    if (size > MAX_SIZE || size == 0)
        return nullptr;

    if (oldp == nullptr) 
        return smalloc(size);

    MallocMetadata *meta_data = (MallocMetadata *)oldp - 1;
    size_t meta_size = meta_data->size;
    if (meta_size >= size)
        return oldp;

    void *newp = smalloc(size);
    if (newp == nullptr)
        return nullptr;

    memcpy(newp, oldp, meta_size);
    sfree(oldp);
    return newp;
}

