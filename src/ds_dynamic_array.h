#ifndef DS_DYNAMIC_ARRAY_H
#define DS_DYNAMIC_ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ----Macros----
// Dynamic array macros start
//
//
//

// Min amount of items
#define DA_MIN_SIZE 128


#define DA_DECLARE(type)                                                       \
  typedef struct {                                                             \
    dynarray_t base;                                                           \
  } type##_da;                                                                 \
                                                                               \
  static inline void type##_da_init(type##_da *da) {                           \
    da_init(&da->base, sizeof(type));                                          \
  }                                                                            \
                                                                               \
  static inline void type##_da_free(type##_da *da) { da_free(&da->base); }     \
                                                                               \
  static inline void type##_da_append(type##_da *da, type value) {             \
    da_append(&da->base, &value);                                              \
  }                                                                            \
                                                                               \
  static inline type type##_da_get(type##_da *da, size_t i) {                  \
    return *(type *)da_get(&da->base, i);                                      \
  }                                                                            \
                                                                               \
  static inline void type##_da_set(type##_da *da, size_t i, type v) {          \
    da_set(&da->base, i, &v);                                                  \
  }

// Dynamic array macros end
//
//
//

#ifdef __cplusplus
extern "C" {
#endif

// Dynamic array declarations start
//
//
//

typedef struct {
  void *items;
  size_t item_size;
  size_t count;
  size_t capacity;
} dynarray_t;

extern void da_init(dynarray_t *da, size_t elem_size);
extern void da_free(dynarray_t *da);
extern void da_append(dynarray_t *da, const void *item);
extern void da_remove(dynarray_t *da, size_t idx);
extern void da_swap(dynarray_t *da, size_t x, size_t y);
extern void *da_pop(dynarray_t *da);
extern void da_swap_and_pop(dynarray_t *da, size_t idx);
extern void *da_get(dynarray_t *da, size_t idx);
extern void da_set(dynarray_t *da, size_t idx, const void *item);

// Dynamic array declarations end
//
//
//

#ifdef __cplusplus
}
#endif

#endif // DS_DYNAMIC_ARRAY_H

// Implementations
// #define DYNAMIC_ARRAY_IMPLEMENTATION
#ifdef DYNAMIC_ARRAY_IMPLEMENTATION

void da_init(dynarray_t *da, size_t item_size) {
  da->count = 0;
  da->capacity = 0;
  da->item_size = item_size;
  da->items = 0;
}

void da_free(dynarray_t *da) {
  free(da->items);
  da->count = 0;
  da->capacity = 0;
  da->item_size = 0;
}

void da_resize(dynarray_t *da) {
  if (da->count < da->capacity / 2 && da->capacity > DA_MIN_SIZE) {
    da->capacity /= 2;
    // TODO(Osdare): handle this better
    if (!realloc(da->items, da->capacity * da->item_size)) {
        return;
    }
  }
}

void da_append(dynarray_t *da, const void *item) {
  if (da->count >= da->capacity) {
    if (da->capacity == 0)
      da->capacity = DA_MIN_SIZE;
    else
      da->capacity *= 2;
    da->items = realloc(da->items, da->capacity * da->item_size);
  }
  memcpy((char *)da->items + da->count * da->item_size, item, da->item_size);
  da->count++;
}

void da_remove(dynarray_t *da, size_t idx) {
  if (idx >= da->count)
    return;

  if (idx != da->count - 1) {
    void *src = (char *)da->items + (idx + 1) * da->item_size;

    void *dst = (char *)da->items + idx * da->item_size;

    size_t bytes_to_move = (da->count - idx - 1) * da->item_size;

    memmove(dst, src, bytes_to_move);
  }
  da->count--;

}

void da_swap(dynarray_t *da, size_t x, size_t y) {
  if (x >= da->count || y >= da->count) {
    return;
  }

  char *xx = (char *)da->items + x * da->item_size;
  char *yy = (char *)da->items + y * da->item_size;

  memcpy(xx, yy, da->item_size);
}

void *da_pop(dynarray_t *da) {
  if (da->count == 0)
    return 0;

  da->count--;
  void *last = (char *)da->items + da->count * da->item_size;

  return last;
}

void da_swap_and_pop(dynarray_t *da, size_t idx) {
  if (idx >= da->count)
    return;

  if (idx != da->count - 1) {
    da_swap(da, idx, da->count - 1);
  }
  da->count--;

}

void *da_get(dynarray_t *da, size_t idx) {
  if (idx >= da->count)
    return 0;

  return (char *)da->items + idx * da->item_size;
}

void da_set(dynarray_t *da, size_t idx, const void *item) {
  if (idx >= da->count)
    return;

  memcpy((char *)da->items + idx * da->item_size, item, da->item_size);
}

#endif // DYNAMIC_ARRAY_IMPLEMENTATION
