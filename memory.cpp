#define _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING

#include "memory.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
//#include <string_view>
#include <utility>
#include <vector>
#include <unordered_set>

#ifdef _MSC_VER
#include <wrl/wrappers/corewrappers.h>
#endif

// Define when using C++ benchmark - so root API calls don't get inlined 
// (similarly like Haskell FFI prevents inlining)
#ifdef PREVENT_INLINE
#ifdef _MSC_VER
#define NOINLINE  __declspec(noinline)
#else
#define NOINLINE  __attribute__ ((noinline)) 
#endif
#else
#define NOINLINE
#endif

// Naive malloc-based manager for comparison in benchmarks
class MemoryManagerC
{
	const size_t itemSize;
public:
	MemoryManagerC(size_t itemSize) : itemSize(itemSize) {}

	void *newItem()             noexcept { return std::malloc(itemSize); }
	void deleteItem(void *item) noexcept { return std::free(item);       }
};

namespace locking_policy
{
	struct NoLocks
	{
		struct Guard {};
		Guard lock() { return {}; };
	};

	struct StdMutex
	{
		std::mutex mx;
		auto lock() { return std::unique_lock<std::mutex>(mx); }
	};

	// TODO implement directly on WinAPI for MinGW
	#ifdef _MSC_VER
	struct SRWLock
	{
		Microsoft::WRL::Wrappers::SRWLock mx;
		auto lock() { return mx.LockExclusive(); }
	};
	#endif

	struct Spinlock
	{
	private:
		enum LockState { Locked, Unlocked };
		std::atomic<LockState> state{ Unlocked };

		struct Guardian
		{
			Spinlock *s{};
			Guardian(Spinlock *s) : s(s) {}
			Guardian(const Guardian &) = delete;
			Guardian(Guardian &&rhs)
				: s(rhs.s)
			{
				rhs.s = nullptr;
			}
			~Guardian() 
			{
				if(s)
					s->state.store(Unlocked, std::memory_order_release);
			}
		};
	public:

		auto lock()
		{
			while(state.exchange(Locked, std::memory_order_acquire) == Locked)
			{ /* busy-wait */ }
			return Guardian{this};
		}
	};
}

template<typename LockPolicy>
class MemoryManager
{
	struct Block
	{
		size_t unitializedItems = 0;
		void *memory = nullptr;

		Block(size_t itemSize, size_t itemsPerBlock)
			: memory(std::malloc(itemSize * itemsPerBlock))
			, unitializedItems(itemsPerBlock)
		{
			if(!memory)
				throw std::runtime_error("out of memory");
		}

		Block(const Block &) = delete;
		Block(Block &&rhs)
			: unitializedItems(rhs.unitializedItems)
			, memory(rhs.memory)
		{
			rhs.memory = nullptr;
		}

		Block& operator=(const Block &) = delete;

		void *itemAtIndex(size_t itemSize, size_t index) const
		{
			return static_cast<char*>(memory) + index * itemSize;
		}

		~Block()
		{
			std::free(memory);
		}
	};

	const size_t itemSize;
	const size_t itemsPerBlock;
	const size_t blockSize = itemSize * itemsPerBlock;

	//boost::container::small_vector<Block, 200> blocks;
	std::vector<Block> blocks;
	void * head = nullptr; // points to the last free, initialized element
	//std::vector<void*> freed;

	LockPolicy mx;
	//boost::mutex mx;


	decltype(auto) addBlock()
	{
		blocks.emplace_back(itemSize, itemsPerBlock);
		return blocks.back();
	}

	void *&storedPtr(void *item) const
	{
		return *static_cast<void**>(item);
	}

	Block &getBlock(void *item)
	{
		for(auto &block : blocks)
			if(belongsTo(block, item))
				return block;

		throw std::runtime_error("cannot find block for item");
	}

	bool belongsTo(const Block &block, void *item)
	{
		return block.memory <= item && static_cast<char*>(block.memory) + blockSize > item;
	}
	
	void *obtainUnitializedItem(Block &block)
	{
		return obtainUnitializedItems(block, 1);
	}

	void *obtainUnitializedItems(Block &block, size_t count)
	{
		assert(block.unitializedItems >= count);
		const auto ret = block.itemAtIndex(itemSize, itemsPerBlock - block.unitializedItems);
		block.unitializedItems -= count;
		return ret;
	}

public:
	MemoryManager(size_t itemSize, size_t itemsPerBlock)
		: itemSize(itemSize)
		, itemsPerBlock(itemsPerBlock)
	{
		if(itemSize < sizeof(void*))
			throw std::runtime_error("item size must be at least of size of a pointer");

		addBlock();
	}

	void *newItem() 
	{
		const auto lockGuard = mx.lock();
		if(head != nullptr)
		{
			const auto ret = head;
			head = storedPtr(head);
			return ret;
		}
		else
		{
			auto &lastBlock = blocks.back();
			if(lastBlock.unitializedItems > 0)
				return obtainUnitializedItem(lastBlock);
			else
				return obtainUnitializedItem(addBlock());
		}
	}

	void *newItems(size_t count)
	{
		const auto lockGuard = mx.lock();
		assert(count <= itemsPerBlock);
		for(auto &block : blocks)
			if(block.unitializedItems >= count)
				return obtainUnitializedItems(block, count);

		return obtainUnitializedItems(addBlock(), count);
	}

	void deleteItem(void *item)
	{
		const auto lockGuard = mx.lock();
 		storedPtr(item) = head;
 		head = item;
	}

	auto allocatedItems() const
	{
		const auto lockGuard = mx.lock();
		const auto addItemsFromBlock = [this] (auto &out, const Block &block) -> void
		{
			const auto perhapsUsedInBlock = itemsPerBlock - block.unitializedItems;
			for(auto i = 0u; i < perhapsUsedInBlock; ++i)
				out.insert(out.end(), block.itemAtIndex(itemSize, i));
		};

		std::unordered_set<void*> ret;
		
		// add all allocated
		for(const Block &block : blocks)
			addItemsFromBlock(ret, block);

		// remove allocated and then freed: 
		// iterate over the free pointers list
		auto itr = this->head;
		while(itr != nullptr)
		{
			ret.erase(itr);
			itr = storedPtr(itr);
		}

		return ret;
	}
};

template<typename F, typename ...Args>
static auto duration(F&& func, Args&&... args)
{
	const auto start = std::chrono::steady_clock::now();
	std::invoke(std::forward<F>(func), std::forward<Args>(args)...);
	return std::chrono::steady_clock::now() - start;
}

template<typename F, typename ...Args>
static auto measure(std::string text, F&& func, Args&&... args)
{
	const auto t = duration(std::forward<F>(func), std::forward<Args>(args)...);
	std::cout << text << " took " << std::chrono::duration_cast<std::chrono::milliseconds>(t).count() << " ms" << std::endl;
	return t;
}

using Action = int; // index of item to delete or -1 to create an item

static std::vector<Action> generateRandomizedActions(int N, double createProbability)
{
	int toCreate = N;
	int existingItems = 0;
	std::vector<Action> actions; // -1 means obtain new item, positive value means delete by index
	actions.reserve(N * 2);

	std::default_random_engine generator; // TODO allow passing seed so values are repeatable (at least for a given stdlib implementation)
	std::bernoulli_distribution distribution(createProbability);

	while(toCreate || existingItems)
	{
		if(toCreate && (distribution(generator) || !existingItems))
		{
			actions.emplace_back(-1);
			--toCreate;
			++existingItems;
		}
		else
		{
			// pick random index
			const auto index = std::uniform_int_distribution<>(0, existingItems - 1)(generator);
			actions.push_back(index);
			--existingItems;
		}
	}
	return actions;
}

template<typename Manager>
static void execute(Manager &mgr, std::vector<void*> &items, const std::vector<Action> &actions)
{
	for(auto action : actions)
	{
		if(action < 0)
			items.push_back(mgr.newItem());
		else
		{
			mgr.deleteItem(items[action]);
			std::swap(items[action], items.back());
			items.pop_back();
		}
	}
}

template<typename Manager>
struct Test
{
private:
	NOINLINE static void *newManager(size_t itemSize, size_t itemsPerBlock)
	{
		return new Manager(itemSize, itemsPerBlock);
	}
	NOINLINE static void deleteManager(void *manager)
	{
		delete static_cast<Manager*>(manager);
	}

	static void *newItem(void *manager)
	{
		return static_cast<Manager*>(manager)->newItem();
	}
	static void deleteItem(void *manager, void *item)
	{
		static_cast<Manager*>(manager)->deleteItem(item);
	}

	static void execute(void *manager, std::vector<void*> &items, const std::vector<Action> &actions)
	{
		::execute(*static_cast<Manager*>(manager), items, actions);
	}

public:
	void test(std::string text, int N, size_t size, size_t itemsPerBlock)
	{
		const auto actions = generateRandomizedActions(N, 0.7);

		std::vector<void*> items(N);
		for(int i = 0; i < 10; i++)
		{
			measure(text + " alloc+free sequence", [&]
			{
				const auto mgr = newManager(size, itemsPerBlock);
				for(int i = 0; i < N; i++)
				{
					const auto item = newItem(mgr);
					//std::memset(item, 0, size);
					deleteItem(mgr, item);
				}
				deleteManager(mgr);
			});

			items.resize(N);
			measure(text + " all allocs; all frees", [&]() mutable
			{
				const auto mgr = newManager(size, itemsPerBlock);
				for(auto &ptr : items)
					ptr = newItem(mgr);
				for(auto &ptr : items)
					deleteItem(mgr, ptr);
				deleteManager(mgr);
			});

			items.clear();
			measure(text + " random", [&]() mutable
			{
				const auto mgr = newManager(size, itemsPerBlock);
				execute(mgr, items, actions);
				deleteManager(mgr);
			});
		}
	}

	void threadedTest(std::string text, int N, size_t size)
	{
		auto threadActions = generateRandomizedActions(N, 0.7);
		for(int i = 0; i < 10; i++)
		{
			const auto mgr = newManager(size);
			std::atomic_int readyThreads{ 0 };
			std::vector<std::thread> threads;
			for(int j = 0; j < 4; j++)
			{
				threads.emplace_back([&]
				{
					readyThreads++;
					while(readyThreads.load() != 4)
						; // busy wait

					std::vector<void*> threadItems;
					threadItems.reserve(N);
					measure(text + " random", [&]
					{
						execute(mgr, threadItems, threadActions);
					});
				});
			}

			for(auto &t : threads)
				t.join();

			deleteManager(mgr);
		}
	}
};

// Note: rough performance results (for alloc N elements, then free N elements benchmark)
//       of different locking policies across platforms:
//
//               | std::mutex | spinlock
//---------------+------------+-----------
// Windows VS    | 443 ms     | 107 ms
// Windows MinGW | 248 ms     | 144 ms
// Linux GCC     | 12 ms      | 220 ms
//---------------+------------+-----------
// Therefore, Windows uses spinlock, non-Windows uses std::mutex
#ifdef _WIN32
using LockingPolicyToUse = locking_policy::Spinlock;
#else
using LockingPolicyToUse = locking_policy::StdMutex;
#endif

using MemoryManagerToUse = MemoryManager<LockingPolicyToUse>;


extern "C"
{
	void *newManager(size_t itemSize, size_t itemsPerBlock)
	{
		return new MemoryManagerToUse(itemSize, itemsPerBlock);
	}
	void deleteManager(void *manager)
	{
		delete static_cast<MemoryManagerToUse*>(manager);
	}
	void *newItem(void *manager)
	{
		return static_cast<MemoryManagerToUse*>(manager)->newItem();
	}
	void *newItems(void *manager, size_t count)
	{
		return static_cast<MemoryManagerToUse*>(manager)->newItems(count);
	}
	void deleteItem(void *manager, void *item)
	{
		static_cast<MemoryManagerToUse*>(manager)->deleteItem(item);
	}

	void** acquireItemList(void *manager, size_t *outCount)
	{
		const auto items = static_cast<MemoryManagerToUse*>(manager)->allocatedItems();
		const auto count = items.size();

		void** ret = static_cast<void**>(std::malloc(sizeof(void*) * count));
		if(!ret)
			return nullptr;
		
		*outCount = count;
		void **itr = ret;
		for(auto item : items)
			*itr++ = item;

		return ret;
	}

	void releaseItemList(void **items)
	{
		std::free(items);
	}

	// BELOW APIs are for tests / benchmarks only //////////////////
	void benchmark(size_t N, size_t itemSize, size_t itemsPerBlock)
	{
		Test<MemoryManagerToUse> test;
		test.test("c++ memory manager", N, itemSize, itemsPerBlock);
	}
	void randomizedBenchmark(size_t N, size_t itemSize, double probability)
	{
		auto actions = generateRandomizedActions(N, probability);
		std::vector<void*> items;
		items.reserve(N);

		MemoryManagerToUse mgr{itemSize, 1024};
		//measure("inner", ::execute<MemoryManagerToUse>, mgr, items, actions);
		::execute(mgr, items, actions);

	}
	void *justReturn(size_t id)
	{
		return reinterpret_cast<void*>(id); // pure evil
	}

}

#ifdef CPP_MAIN
int main()
{
	// std::cout << "Will run benchmark (unless optimized away)\n";
	// benchmark(10'000'000, 64);
	for(int i = 0; i < 10; i++)
		measure("randomized pattern", randomizedBenchmark, 10000000, 50, 0.7);
}
#endif