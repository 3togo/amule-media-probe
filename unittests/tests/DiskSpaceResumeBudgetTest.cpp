// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include <muleunit/test.h>
#include "DiskSpaceResumeBudget.h"
#include <limits>

using namespace muleunit;

DECLARE_SIMPLE(DiskSpaceResumeBudget)

TEST(DiskSpaceResumeBudget, NeededSpaceLargerThanFreeDoesNotWrap)
{
	CDiskSpaceResumeBudget budget(100, 10);
	bool called = false;
	ASSERT_FALSE(budget.TryResume(101, [&called] {
		called = true;
		return true;
	}));
	ASSERT_FALSE(called);
	ASSERT_TRUE(budget.TryResume(89, [] { return true; }));
}

TEST(DiskSpaceResumeBudget, MultipleFilesCannotSpendTheSameFreeSpace)
{
	CDiskSpaceResumeBudget budget(100, 10);
	ASSERT_TRUE(budget.TryResume(60, [] { return true; }));
	ASSERT_FALSE(budget.TryResume(60, [] { return true; }));
	ASSERT_TRUE(budget.TryResume(29, [] { return true; }));
	ASSERT_FALSE(budget.TryResume(1, [] { return true; }));
}

TEST(DiskSpaceResumeBudget, FailedResumeLeavesSpaceForTheNextFile)
{
	CDiskSpaceResumeBudget budget(100, 10);
	ASSERT_FALSE(budget.TryResume(80, [] { return false; }));
	ASSERT_TRUE(budget.TryResume(89, [] { return true; }));
}

TEST(DiskSpaceResumeBudget, PreservesStrictMinimumFreeSpaceBoundary)
{
	for (uint64_t free : { 0u, 9u, 10u }) {
		CDiskSpaceResumeBudget budget(free, 10);
		ASSERT_FALSE(budget.TryResume(0, [] { return true; }));
	}
	CDiskSpaceResumeBudget budget(100, 10);
	ASSERT_FALSE(budget.TryResume(90, [] { return true; }));
	ASSERT_TRUE(budget.TryResume(89, [] { return true; }));
	ASSERT_TRUE(budget.TryResume(0, [] { return true; }));
}

TEST(DiskSpaceResumeBudget, MaximumValuesCannotOverflowThreshold)
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	CDiskSpaceResumeBudget budget(maximum, maximum - 10);
	ASSERT_FALSE(budget.TryResume(maximum, [] { return true; }));
	ASSERT_FALSE(budget.TryResume(10, [] { return true; }));
	ASSERT_TRUE(budget.TryResume(9, [] { return true; }));
}
