#ifndef SOL_ALLOCATOR_HPP_INCLUDE_GUARD_
#define SOL_ALLOCATOR_HPP_INCLUDE_GUARD_

#include "tlsf.h"
#include "typedef.h"
#include "assert.h"

static inline size_t align(size_t size, size_t alignment) {
  const size_t alignment_mask = alignment - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

struct Heap_Allocator {
    u64 capacity;
    u64 used;
    u8 *memory;
    void *tlsf_handle;
};
struct Linear_Allocator {
    u64 capacity;
    u64 used;
    u8 *memory;
};
                           /* ** Begin Global Allocators ** */

        /* Every function in this section applies to the two global allocators. */

// Call for instances of the global allocators.
// These exist for the lifetime of the program.
Heap_Allocator   *get_instance_heap();
Linear_Allocator *get_instance_temp();

void init_allocators();
void kill_allocators();

void init_heap_allocator(u64 size);
void init_temp_allocator(u64 size);

void kill_heap_allocator();
void kill_temp_allocator();

u8 *malloc_h(u64 size, u64 alignment = 16); // Make heap allocation
u8 *realloc_h(void *ptr, u64 new_size);  // Reallocate a heap allocation
u8 *malloc_t(u64 size, u64 alignment = 16); // Make a temporary allocation
u8 *realloc_t(void *ptr, u64 new_size, u64 old_size, u64 alignment); // Reallocate temp allocation (malloc_t + memcpy)

inline void free_h(void *ptr) { // Free a heap allocation
    u64 size = tlsf_block_size(ptr);

    Heap_Allocator *allocator = get_instance_heap();
    assert(size <= allocator->used && "Heap Allocator Underflow");
    allocator->used -= size;
    tlsf_free(allocator->tlsf_handle, ptr);
}

// Manipulate temp allocator
inline static u64 get_used_temp() {
    return get_instance_temp()->used;
}
inline static void align_temp(u64 size) {
    Linear_Allocator *temp = get_instance_temp();
    temp->used = align(temp->used, size);
}
static inline void reset_temp() {
    get_instance_temp()->used = 0;
}
static inline void zero_temp() {
    Linear_Allocator *temp = get_instance_temp();
    memset(temp->memory, 0, temp->used);
    temp->used = 0;
}
static inline void cut_tail_temp(u64 size) {
    get_instance_temp()->used -= size;
}
static inline void reset_to_mark_temp(u64 size) {
    get_instance_temp()->used = size;
}
static inline u64 get_mark_temp() {
    return get_instance_temp()->used;
}

#endif // include guard
