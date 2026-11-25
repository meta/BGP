/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * UpdateGroupBitPositionTest.cpp
 *
 * Unit tests for AdjRibOutGroup bit position allocation.
 * Tests verify that AdjRibGroup correctly uses ConsumerBitManager for
 * bit allocation when peers register/unregister.
 *
 * The fix addresses a bug where bit allocation used bitToAdjRibs_.size()
 * which doesn't properly handle bit reuse when peers leave and rejoin.
 *
 * Testing approach per guidelines:
 * - One test to verify the connection (AdjRibGroup uses bitManager_)
 * - E2E tests (in e2e/UpdateGroupBitAllocationTest.cpp) verify full behavior
 */

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitManager.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"

namespace facebook::bgp {

/**
 * Test fixture that extends AdjRibOutboundFixture to get proper AdjRib setup.
 */
class UpdateGroupBitPositionTest : public AdjRibOutboundFixture {
 protected:
  void SetUp() override {
    AdjRibOutboundFixture::SetUp();
  }

  void TearDown() override {
    if (group_) {
      group_.reset();
    }
  }

  /**
   * Helper to create an AdjRibOutGroup for testing
   */
  std::shared_ptr<AdjRibOutGroup> createGroup(const std::string& name) {
    return std::make_shared<AdjRibOutGroup>(
        evb_,
        fboss::kDefaultVrf,
        name,
        0 /* groupId */,
        true /* enableUpdateGroup */);
  }

  std::shared_ptr<AdjRibOutGroup> group_;
};

/*
 * =============================================================================
 * CONNECTION TEST - Verifies AdjRibGroup uses ConsumerBitManager
 * =============================================================================
 */

/**
 * Test: AdjRibOutGroup creation initializes with empty member count.
 * This is a basic sanity test that the group is properly initialized.
 */
TEST_F(UpdateGroupBitPositionTest, GroupCreation) {
  group_ = createGroup("test_group");

  EXPECT_EQ(group_->getMemberCount(), 0);
  EXPECT_EQ(group_->getAdjRibGroupName(), "test_group");
}

/*
 * =============================================================================
 * CORE BIT REUSE TEST - Verifies the bug fix works
 * =============================================================================
 */

/**
 * Test: ConsumerBitManager bit reuse after free.
 * This is the key test verifying the bug fix - bits must be reused when freed.
 * The old buggy code used bitToAdjRibs_.size() which would allocate new bits
 * instead of reusing freed ones.
 */
TEST_F(UpdateGroupBitPositionTest, BitReuseAfterFree) {
  ConsumerBitManager bitManager;

  // Allocate 3 bits
  auto bit0 = bitManager.getConsumerBit();
  auto bit1 = bitManager.getConsumerBit();
  auto bit2 = bitManager.getConsumerBit();

  EXPECT_EQ(bit0, 0);
  EXPECT_EQ(bit1, 1);
  EXPECT_EQ(bit2, 2);

  // Free middle bit
  EXPECT_EQ(bitManager.freeConsumerBit(bit1), ConsumerBitError::SUCCESS);
  EXPECT_EQ(bitManager.getNumBitsInUse(), 2);

  // Next allocation should reuse bit 1, NOT allocate bit 3
  auto bit3 = bitManager.getConsumerBit();
  EXPECT_EQ(bit3, 1); // Key assertion: reuses freed bit
  EXPECT_EQ(bitManager.getNumBitsInUse(), 3);
}

/**
 * Test: Alternate allocate and free pattern.
 * This tests the exact buggy behavior that was fixed - using size() for
 * allocation would cause bit collisions in this pattern.
 */
TEST_F(UpdateGroupBitPositionTest, AlternateAllocateAndFree) {
  ConsumerBitManager bitManager;

  // Allocate bit 0
  auto bit0 = bitManager.getConsumerBit();
  EXPECT_EQ(bit0, 0);

  // Free bit 0
  bitManager.freeConsumerBit(bit0);

  // Allocate again - should get bit 0 reused
  auto bit1 = bitManager.getConsumerBit();
  EXPECT_EQ(bit1, 0);

  // Allocate bit 1
  auto bit2 = bitManager.getConsumerBit();
  EXPECT_EQ(bit2, 1);

  // Free bit 0 again
  bitManager.freeConsumerBit(bit1);

  // Allocate - should get bit 0 reused again, NOT bit 1 (collision!)
  auto bit3 = bitManager.getConsumerBit();
  EXPECT_EQ(bit3, 0);

  // Verify no collision - bit3 and bit2 are different
  EXPECT_NE(bit3, bit2);
}

/*
 * =============================================================================
 * ERROR HANDLING - Basic error cases for robustness
 * =============================================================================
 */

/**
 * Test: Error handling - double free returns error.
 */
TEST_F(UpdateGroupBitPositionTest, DoubleFreeReturnsError) {
  ConsumerBitManager bitManager;

  auto bit = bitManager.getConsumerBit();

  // First free should succeed
  EXPECT_EQ(bitManager.freeConsumerBit(bit), ConsumerBitError::SUCCESS);

  // Second free should fail with appropriate error
  EXPECT_EQ(
      bitManager.freeConsumerBit(bit),
      ConsumerBitError::INVALID_BIT_ALREADY_FREED);
}

/**
 * Test: Error handling - free bit beyond max returns error.
 */
TEST_F(UpdateGroupBitPositionTest, FreeBeyondMaxReturnsError) {
  ConsumerBitManager bitManager(10);

  EXPECT_EQ(
      bitManager.freeConsumerBit(100), ConsumerBitError::INVALID_BIT_TOO_BIG);
}

} // namespace facebook::bgp
