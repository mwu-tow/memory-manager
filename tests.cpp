#define BOOST_TEST_MODULE mytests
#include "memory.h"

#include <random>
#include <unordered_set>

#include <boost/test/included/unit_test.hpp>

namespace
{
	// return C++ container
	auto getItems(void *manager)
	{
		size_t size = 0u;
		std::unique_ptr<void*, void(*)(void**)> itemsArray(acquireItemList(manager, &size), &releaseItemList);
		return std::unordered_set<void*>(itemsArray.get(), itemsArray.get() + size);
	}

	template<typename Functor, typename ...Args>
	void repeatN(size_t N, Functor &&f, Args && ...args)
	{
		for(size_t i = 0u; i < N; ++i)
			std::invoke(f, args...);
	}

	template<typename Range>
	auto select_randomly(Range &&range) 
	{
		const auto size = std::size(range);
		if(size == 0u)
			return std::end(range);

		static std::random_device device;
		static std::mt19937 generator{ device() };
		const std::uniform_int_distribution<size_t> distribution(0u, size - 1u);
		return std::next(std::begin(range), distribution(generator));
	}
}

BOOST_AUTO_TEST_CASE(ObtainingActiveItemsList)
{
	const auto blockSize = 250;
	auto manager = newManager(50, blockSize);

	std::unordered_set<void*> myKnownItems;
	const auto allocateItem = [&] { myKnownItems.insert(newItem(manager)); };
	const auto deleteRandomItem = [&]
	{
		const auto itemItr = select_randomly(myKnownItems);
		deleteItem(manager, *itemItr);
		myKnownItems.erase(itemItr);
	};

	// check if library lists the same elements as we have saved when allocating
	const auto verify = [&](const std::string &contextText)
	{
		BOOST_TEST_CONTEXT(contextText) 
		{
			const auto reportedItems = getItems(manager);
			BOOST_CHECK(myKnownItems == reportedItems); 
		}
	};

	verify("empty on start"); 
	// allocate 400 element
	repeatN(400, allocateItem);
	BOOST_REQUIRE_EQUAL(myKnownItems.size(), 400);
	verify("after initial 400 elem allocation");

	// delete random 100 elements
	repeatN(100, [&] 
	{
		const auto itemItr = select_randomly(myKnownItems);
		deleteItem(manager, *itemItr);
		myKnownItems.erase(itemItr);
	});
	BOOST_REQUIRE_EQUAL(myKnownItems.size(), 300);
	verify("allocated 400, deleted 100");

	repeatN(75, allocateItem);
	BOOST_REQUIRE_EQUAL(myKnownItems.size(), 375);
	verify("allocated 400, deleted 100, allocated 75");

	repeatN(75, allocateItem);
	BOOST_REQUIRE_EQUAL(myKnownItems.size(), 450);
	verify("allocated 400, deleted 100, allocated 150");

	while(myKnownItems.empty() == false)
		deleteRandomItem();

	verify("after deleting all remaining elements");

	deleteManager(manager);
}