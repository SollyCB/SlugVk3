#pragma once

#include <immintrin.h>

#include "builtin_wrappers.h"
#include "basic.h"
#include "external/wyhash.h"
#include "allocator.hpp"

#if TEST
#include "test/test.hpp"
#endif

const u8 EMPTY = 0b1111'1111;
const u8 DEL   = 0b1000'0000;
const u8 GROUP_WIDTH = 16;

template <typename T>
inline uint64_t calculate_hash(const T &value, size_t seed = 0) {
    return wyhash(&value, sizeof(T), seed, _wyp);
}

template <size_t N>
inline uint64_t calculate_hash(const char (&value)[N], size_t seed = 0) {
    return wyhash(value, strlen(value), seed, _wyp);
}

inline uint64_t calculate_hash(const char *value, size_t seed) {
    return wyhash(value, strlen(value), seed, _wyp);
}

inline u64 get_string_hash(String *string) {
    return wyhash(string->str, string->len, 0, _wyp);
}

inline uint64_t hash_bytes(void *data, size_t len, size_t seed = 0) {
    return wyhash(data, len, seed, _wyp);
}

inline void checked_mul(size_t &res, size_t mul) {
    assert(UINT64_MAX / res > mul && "u64 checked mul overflow");
    res = res * mul;
}

inline void make_group_empty(uint8_t *bytes) {
    __m128i ctrl = _mm_set1_epi8(EMPTY);
    _mm_store_si128(reinterpret_cast<__m128i *>(bytes), ctrl);
}

struct Group {
    __m128i ctrl;

    static inline Group get_from_index(u64 index, u8 *data) {
        Group ret;
        ret.ctrl = *reinterpret_cast<__m128i*>(data + index);
        return ret;
    }
    static inline Group get_empty() {
        Group ret;
        ret.ctrl = _mm_set1_epi8(EMPTY);
        return ret;
    }
    inline u16 is_empty() {
        __m128i empty = _mm_set1_epi8(EMPTY);
        __m128i res = _mm_cmpeq_epi8(ctrl, empty);
        u16 mask = _mm_movemask_epi8(res);
        return mask;
    }
    inline u16 is_special() {
        uint16_t mask = _mm_movemask_epi8(ctrl);
        return mask;
    }
    inline u16 is_full() {
        u16 mask = is_special();
        uint16_t invert = ~mask;
        return invert;
    }
    inline void fill(uint8_t *bytes) {
        _mm_store_si128(reinterpret_cast<__m128i *>(bytes), ctrl);
    }
    inline u16 match_byte(uint8_t byte) {
        __m128i to_match = _mm_set1_epi8(byte);
        __m128i match = _mm_cmpeq_epi8(ctrl, to_match);
        return (uint16_t)_mm_movemask_epi8(match);
    }
};

template<typename K, typename V>
struct HashMap {

    struct KeyValue {
        K key;
        V value;
    };

    struct Iter {
        u64 current_pos;
        HashMap<K, V> *map;

        KeyValue* next() {
			u8 pos_in_group;
			u16 mask;
			u32 tz;
			u64 group_index;
			Group gr;
			KeyValue *kv;
			while(current_pos < map->cap) {

				pos_in_group = current_pos & (GROUP_WIDTH - 1);
				group_index = current_pos - pos_in_group;

				gr = *(Group*)(map->data + group_index);
				mask = gr.is_full();
				mask >>= pos_in_group;

				if (mask) {
					tz = count_trailing_zeros_u16(mask);
					current_pos += tz;
					kv = (KeyValue*)(map->data + map->cap + (current_pos * sizeof(KeyValue)));

					++current_pos;
					return kv;
				}

				current_pos += GROUP_WIDTH - pos_in_group;
			}
			return NULL;
        }
    };
    Iter iter() {
        Iter ret = {0, this};
        return ret;
    };

    u64 cap; // in key-value pairs
    u64 slots_left; // in key-value pairs
    u8 *data;

    static inline HashMap<K, V> get(u64 initial_cap) {
        HashMap<K, V> ret;
        ret.init(initial_cap);
        return ret;
    }
    void init(u64 initial_cap) {
        cap        = align(initial_cap, GROUP_WIDTH);
        slots_left = ((cap + 1) / 8) * 7;
        data       = malloc_h(cap * sizeof(KeyValue) + cap, GROUP_WIDTH);
        for(u64 i = 0; i < cap; i += GROUP_WIDTH) {
            make_group_empty(data + i);
        }
    }

    bool insert_cpy(K key, V value) {
        if (slots_left == 0) {
            u8 *old_data = data;
            u64 old_cap = cap;

            checked_mul(cap, 2);
            data = malloc_h(cap + cap * sizeof(KeyValue), GROUP_WIDTH);
            slots_left = ((cap + 1) / 8) * 7;

            for(u64 i = 0; i < cap; i += GROUP_WIDTH) {
                make_group_empty(data + i);
            }

            bool c;
            Group gr;
            KeyValue *kv;
            u16 mask;
            u32 tz;
            for(u64 group_index = 0; group_index < old_cap; group_index += GROUP_WIDTH) {
                gr = Group::get_from_index(group_index, old_data);

                mask = gr.is_full();
                while(mask > 0) {
                    tz = count_trailing_zeros_u16(mask);

                    // @FFS This was fking set to '&=' cos I am dumb and tired which means
                    // infinite loop lol... I fking hate and love programming
                    mask ^= 1 << tz;

                    kv = (KeyValue*)(old_data + old_cap);

                    // @Note Assumes this compiles to just the function call
                    c = insert_cpy(kv[group_index + tz].key , kv[group_index + tz].value);
                    assert(c && "Rehash Failure");
                }
            }

            free_h(old_data);
        }

        u64 hash = hash_bytes((void*)&key, sizeof(key));
        u8 top7 = hash >> 57;

        u64 exact_index = (hash & (cap - 1));
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            assert(inc <= cap && "Probe went too far");
            gr = Group::get_from_index(group_index, data);
            mask = gr.is_empty();

            if (!mask) {
                inc += GROUP_WIDTH;
                group_index += inc;
                group_index &= cap - 1;
                continue;
            }

            tz = count_trailing_zeros_u16(mask);
            exact_index = group_index + tz;
            data[exact_index] &= top7;

            kv = (KeyValue*)(data + cap);
            kv[exact_index].key = key;
            kv[exact_index].value = value;

            --slots_left;
            return true;
        }

        return false;
    }
    bool insert_ptr(K *key, V *value) {
		assert(key != nullptr && "pass key == nullptr to HashMap::insert_ptr");
		assert(value != nullptr && "pass value == nullptr to HashMap::insert_ptr");

        if (slots_left == 0) {
            u8 *old_data = data;
            u64 old_cap = cap;

            checked_mul(cap, 2);
            data = malloc_h(cap + cap * sizeof(KeyValue), GROUP_WIDTH);
            slots_left = ((cap + 1) / 8) * 7;

            for(u64 i = 0; i < cap; i += GROUP_WIDTH) {
                make_group_empty(data + i);
            }

            bool c;
            KeyValue *kv;
            Group gr;
            u16 mask;
            u32 tz;
            for(u64 group_index = 0; group_index < old_cap; group_index += GROUP_WIDTH) {
                gr = *(Group*)(old_data + group_index);

                mask = gr.is_full();
                while(mask > 0) {
                    tz = count_trailing_zeros_u16(mask);

                    // @FFS This was fking set to '&=' cos I am dumb and tired which means
                    // infinite loop lol... I fking hate and love programming
                    mask ^= 1 << tz;

                    kv = (KeyValue*)(old_data + old_cap);
                    u64 exact_index = tz + group_index;
                    c = insert_ptr(&kv[exact_index].key, &kv[exact_index].value);
                    assert(c && "Rehash Failure");
                }
            }

            free_h(old_data);
        }

        u64 hash = hash_bytes((void*)key, sizeof(*key));
        u8 top7 = hash >> 57;

        u64 exact_index = (hash & (cap - 1));
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            assert(inc < cap && "Probe went too far");
            gr = Group::get_from_index(group_index, data);
            mask = gr.is_empty();

            if (!mask) {
                inc += GROUP_WIDTH;
                group_index += inc;
                group_index &= cap - 1;
                continue;
            }

            tz = count_trailing_zeros_u16(mask);
            exact_index = group_index + tz;
            data[exact_index] &= top7;

            kv = (KeyValue*)(data + cap);
            kv[exact_index].key = *key;
            kv[exact_index].value = *value;

            --slots_left;
            return true;
        }

        return false;
    }
    bool insert_hash(u64 hash, V *value) {
		assert(value != nullptr && "pass value == nullptr to HashMap::insert_ptr");

        bool c;
        if (slots_left == 0) {
            u8 *old_data = data;
            u64 old_cap = cap;

            checked_mul(cap, 2);
            data = malloc_h(cap + cap * sizeof(KeyValue), GROUP_WIDTH);
            slots_left = ((cap + 1) / 8) * 7;

            for(u64 i = 0; i < cap; i += GROUP_WIDTH) {
                make_group_empty(data + i);
            }

            KeyValue *kv;
            Group gr;
            u16 mask;
            u32 tz;
            for(u64 group_index = 0; group_index < old_cap; group_index += GROUP_WIDTH) {
                gr = *(Group*)(old_data + group_index);

                mask = gr.is_full();
                while(mask > 0) {
                    tz = count_trailing_zeros_u16(mask);

                    // @FFS This was fking set to '&=' cos I am dumb and tired which means
                    // infinite loop lol... I fking hate and love programming
                    mask ^= 1 << tz;

                    kv = (KeyValue*)(old_data + old_cap);
                    u64 exact_index = tz + group_index;

                    // @Note Assumes this compiles to just the function call
                    c = insert_hash(kv[exact_index].key, &kv[exact_index].value);
                    assert(c && "Rehash Failure");
                }
            }

            free_h(old_data);
        }

        u8 top7 = hash >> 57;

        u64 exact_index = (hash & (cap - 1));
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            assert(inc < cap && "Probe went too far");
            gr = Group::get_from_index(group_index, data);
            mask = gr.is_empty();

            if (!mask) {
                inc += GROUP_WIDTH;
                group_index += inc;
                group_index &= cap - 1;
                continue;
            }

            tz = count_trailing_zeros_u16(mask);
            exact_index = group_index + tz;
            data[exact_index] &= top7;

            kv = (KeyValue*)(data + cap);
            kv[exact_index].key = hash;
            kv[exact_index].value = *value;

            --slots_left;
            return true;
        }

        return false;
    }

    V* find_cpy(K key) {
        u64 hash = hash_bytes((void*)&key, sizeof(key));
        u8 top7 = hash >> 57;
        u64 exact_index = hash & (cap - 1);
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            gr = Group::get_from_index(group_index, data);
            mask = gr.match_byte(top7);

            if (mask) {
                // Ik the while catches an empty mask, but I want to skip the pointer arithmetic
                kv = (KeyValue*)(data + cap);
                while(mask) {
                    tz = count_trailing_zeros_u16(mask);
                    exact_index = group_index + tz;

                    if (kv[exact_index].key == key)
                        return &kv[exact_index].value;

                    mask ^= 1 << tz;
                }
            }

            inc += GROUP_WIDTH;
            group_index += inc;
            group_index &= cap - 1;
        }
        return NULL;
    }

    V* find_ptr(K *key) {
		assert(key != nullptr && "pass key == nullptr to HashMap::find_ptr");

        u64 hash = hash_bytes((void*)key, sizeof(*key));
        u8 top7 = hash >> 57;
        u64 exact_index = hash & (cap - 1);
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            gr = Group::get_from_index(group_index, data);
            mask = gr.match_byte(top7);

            if (mask) {
                // Ik the while catches an empty mask, but I want to skip the pointer arithmetic
                kv = (KeyValue*)(data + cap);
                while(mask) {
                    tz = count_trailing_zeros_u16(mask);
                    exact_index = group_index + tz;

                    if (kv[exact_index].key == *key)
                        return &kv[exact_index].value;

                    mask ^= 1 << tz;
                }
            }

            inc += GROUP_WIDTH;
            group_index += inc;
            group_index &= cap - 1;
        }
        return NULL;
    }
    V* find_hash(u64 hash) {
        u8 top7 = hash >> 57;
        u64 exact_index = hash & (cap - 1);
        u64 group_index = exact_index - (exact_index & (GROUP_WIDTH - 1));

        KeyValue *kv;
        Group gr;
        u16 mask;
        u32 tz;
        u64 inc = 0;
        while(inc < cap) {
            gr = Group::get_from_index(group_index, data);
            mask = gr.match_byte(top7);

            if (mask) {
                // Ik the while catches an empty mask, but I want to skip the pointer arithmetic
                kv = (KeyValue*)(data + cap);
                while(mask) {
                    tz = count_trailing_zeros_u16(mask);
                    exact_index = group_index + tz;

                    if (kv[exact_index].key == hash)
                        return &kv[exact_index].value;

                    mask ^= 1 << tz;
                }
            }

            inc += GROUP_WIDTH;
            group_index += inc;
            group_index &= cap - 1;
        }
        return NULL;
    }

    void kill() {
        free_h(data);
        cap = 0;
        slots_left = 0;
    }
};
