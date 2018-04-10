This is a provisional project for early development of Memory Manager. Its purpose is to allow trying out, benchmarking and sharing various ideas. The layout and code organization will change.

# To see it running
```
stack build && stack exec -- memt --output report.html
```

# Project structure
Memory Manager sources:
* `memory.h` —  C++ header file defining C API of memory manager
* `memory.cpp` — C++ code with Memory Manager, implementing its API

Other files:
* `src/Main.hs` — Haskell code calling the Memory Manager along with some Criterion benchmarks.
* `test.cpp` — file contaning some test rudimentary test code (dependant on Boost.Test) for the manager, at the moment rather for experiments than code quality.

# Coding guidelines
C++ code is written in C++17 subset that is supported both by VS 2017.5 and GCC 6.2.0. The GCC requirement comes from the MinGW version bundled with Windows GHC distribution package.

# Memory Manager API
Currently supported Memory Manager Core API:
* `void *newManager(size_t itemSize, size_t itemsPerBlock)` — creates a new Manager that will provide items of given size in bytes
* `void deleteManager(void *manager)` — deletes a Manager, freeing all all items that were allocated
* `void *newItem(void *manager)` — returns a pointer uniquely owning a memory of item's size
* `void *newItems(void *manager, size_t count)` — allocates a contiguous sequence of N items, returns a pointer to the first item. Further items can by accessed by incrementing pointer with an `itemSize` (memory manager does not add any alignment). Note: `count` value must be not greater than `itemsPerBlock` value used when `manager` was created! 
* `void deleteItem(void *manager, void *item)` — relinquishes ownership of memory for given item

Currently supported Memory Manager Debug API:
* `void** acquireItemList(void *manager, size_t *outCount)` — allocates and returns a contiguous array containing pointers to all items having been allocated by the given Manager. `outCount` is an output parameter and must point a valid memory that will be written with the length of returned array. The returned pointer points to the array's first element. Returned array must be later freed by the `releaseItemList` call. Element order is not defined. In case of failure (e.g. out of memory) returns `nullptr` and `outCount` is not written.
* `void releaseItemList(void **items)` — releases an array obtained from a `acquireItemList` call. Does nothing if called with a `nullptr`.