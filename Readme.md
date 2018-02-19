This is a provisional project for early development of Memory Manager. Its purpose is to allow trying out, benchmarking and sharing various ideas. The layout and code organization will change.

# To see it running
```
stack build && stack exec -- memt --output report.html
```

# Project structure
Files of interest:
* `memory.cpp` — C++ code with Memory Manager, implementing its API
* `src/Main.hs` — Haskell code calling the Memory Manager along with some Criterion benchmarks

# Coding guidelines
C++ code is written in C++17 subset that is supported both by VS 2017.5 and GCC 6.2.0. The GCC requirement comes from the MinGW version bundled with Windows GHC distribution package.

# Memory Manager API
Currently supported Memory Manager API:
* `void *newManager(size_t itemSize)` — creates a new Manager that will provide items of given size in bytes
* `void deleteManager(void *manager)` — deletes a maneger, freeing all all items that were allocated
* `void *newItem(void *manager)` — returns a pointer uniquely owning a memory of item's size
* `void deleteItem(void *manager, void *item)` — relinquishes ownership of memory for given item
