// Copyright 2016 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/access_revocation_manager_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <weave/provider/test/mock_config_store.h>
#include <weave/test/unittest_utils.h>

#include "src/test/mock_clock.h"
#include "src/bind_lambda.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace weave {

class AccessRevocationManagerImplTest : public testing::Test {
 protected:
  void SetUp() {
    std::string to_load = R"([{
      "user": "BQID",
      "app": "BwQF",
      "expiration": 463315200,
      "revocation": 463314200
    }, {
      "user": "AQID",
      "app": "AwQF",
      "expiration": 473315199,
      "revocation": 473313199
    }])";

    EXPECT_CALL(config_store_, LoadSettings("black_list"))
        .WillOnce(Return(to_load));

    EXPECT_CALL(config_store_, SaveSettings("black_list", _, _))
        .WillOnce(testing::WithArgs<1, 2>(testing::Invoke(
            [](const std::string& json, const DoneCallback& callback) {
              std::string to_save = R"([{
                "user": "AQID",
                "app": "AwQF",
                "expiration": 473315199,
                "revocation": 473313199
              }])";
              EXPECT_JSON_EQ(to_save, *test::CreateValue(json));
              if (!callback.is_null())
                callback.Run(nullptr);
            })));

    EXPECT_CALL(clock_, Now())
        .WillRepeatedly(Return(base::Time::FromTimeT(1412121212)));
    manager_.reset(
        new AccessRevocationManagerImpl{&config_store_, 10, &clock_});
  }
  StrictMock<test::MockClock> clock_;
  StrictMock<provider::test::MockConfigStore> config_store_{false};
  std::unique_ptr<AccessRevocationManagerImpl> manager_;
};

TEST_F(AccessRevocationManagerImplTest, Init) {
  EXPECT_EQ(1u, manager_->GetSize());
  EXPECT_EQ(10u, manager_->GetCapacity());
  EXPECT_EQ((std::vector<AccessRevocationManagerImpl::Entry>{{
                {1, 2, 3},
                {3, 4, 5},
                base::Time::FromTimeT(1419997999),
                base::Time::FromTimeT(1419999999),
            }}),
            manager_->GetEntries());
}

TEST_F(AccessRevocationManagerImplTest, Block) {
  bool callback_called = false;
  manager_->AddEntryAddedCallback(
      base::Bind([](bool* callback_called) { *callback_called = true; },
                 base::Unretained(&callback_called)));
  EXPECT_CALL(config_store_, SaveSettings("black_list", _, _))
      .WillOnce(testing::WithArgs<1, 2>(testing::Invoke(
          [](const std::string& json, const DoneCallback& callback) {
            std::string to_save = R"([{
                "user": "AQID",
                "app": "AwQF",
                "expiration": 473315199,
                "revocation": 473313199
              }, {
                "app": "CAgI",
                "user": "BwcH",
                "expiration": 473305200,
                "revocation": 473295200
              }])";
            EXPECT_JSON_EQ(to_save, *test::CreateValue(json));
            if (!callback.is_null())
              callback.Run(nullptr);
          })));
  manager_->Block({{7, 7, 7},
                   {8, 8, 8},
                   base::Time::FromTimeT(1419980000),
                   base::Time::FromTimeT(1419990000)},
                  {});
  EXPECT_TRUE(callback_called);
}

TEST_F(AccessRevocationManagerImplTest, BlockExpired) {
  manager_->Block({{},
                   {},
                   base::Time::FromTimeT(1300000000),
                   base::Time::FromTimeT(1400000000)},
                  base::Bind([](ErrorPtr error) {
                    EXPECT_TRUE(error->HasError("aleady_expired"));
                  }));
}

TEST_F(AccessRevocationManagerImplTest, BlockListOverflow) {
  EXPECT_CALL(config_store_, LoadSettings("black_list")).WillOnce(Return(""));
  manager_.reset(new AccessRevocationManagerImpl{&config_store_, 10, &clock_});

  EXPECT_CALL(config_store_, SaveSettings("black_list", _, _))
      .WillRepeatedly(testing::WithArgs<1, 2>(testing::Invoke(
          [](const std::string& json, const DoneCallback& callback) {
            if (!callback.is_null())
              callback.Run(nullptr);
          })));

  EXPECT_EQ(0u, manager_->GetSize());

  // Trigger overflow several times.
  for (size_t i = 0; i < manager_->GetCapacity() + 3; ++i) {
    bool callback_called = false;
    manager_->Block({{99, static_cast<uint8_t>(i), static_cast<uint8_t>(i)},
                     {8, 8, 8},
                     base::Time::FromTimeT(1419970000 + i),
                     base::Time::FromTimeT(1419990000)},
                    base::Bind([](bool* callback_called, ErrorPtr error) {
                      *callback_called = true;
                      EXPECT_FALSE(error);
                    }, base::Unretained(&callback_called)));
    EXPECT_TRUE(callback_called);
  }
  EXPECT_EQ(manager_->GetCapacity(), manager_->GetSize());

  // We didn't block these ids, so we can use this to check if all_blocking
  // issue is set for correct revocation time.
  EXPECT_TRUE(manager_->IsBlocked({1}, {2}, base::Time::FromTimeT(1419970003)));
  EXPECT_FALSE(
      manager_->IsBlocked({1}, {2}, base::Time::FromTimeT(1419970004)));

  // Check if all blocking rules still work.
  for (size_t i = 0; i < manager_->GetCapacity() + 3; ++i) {
    EXPECT_TRUE(manager_->IsBlocked(
        {99, static_cast<uint8_t>(i), static_cast<uint8_t>(i)}, {8, 8, 8},
        base::Time::FromTimeT(1419970000 + i)));
  }
}

TEST_F(AccessRevocationManagerImplTest, IsBlockedIdsNotMacth) {
  EXPECT_FALSE(manager_->IsBlocked({7, 7, 7}, {8, 8, 8}, {}));
}

TEST_F(AccessRevocationManagerImplTest, IsBlockedRevocationIsOld) {
  // Ids match but delegation time is newer than revocation time.
  EXPECT_FALSE(manager_->IsBlocked({1, 2, 3}, {3, 4, 5},
                                   base::Time::FromTimeT(1429997999)));
}

class AccessRevocationManagerImplIsBlockedTest
    : public AccessRevocationManagerImplTest,
      public testing::WithParamInterface<
          std::tuple<std::vector<uint8_t>, std::vector<uint8_t>>> {
 public:
  void SetUp() override {
    AccessRevocationManagerImplTest::SetUp();
    EXPECT_CALL(config_store_, SaveSettings("black_list", _, _))
        .WillOnce(testing::WithArgs<2>(
            testing::Invoke([](const DoneCallback& callback) {
              if (!callback.is_null())
                callback.Run(nullptr);
            })));
    manager_->Block({std::get<0>(GetParam()),
                     std::get<1>(GetParam()),
                     {},
                     base::Time::FromTimeT(1419990000)},
                    {});
  }
};

TEST_P(AccessRevocationManagerImplIsBlockedTest, IsBlocked) {
  EXPECT_TRUE(manager_->IsBlocked({7, 7, 7}, {8, 8, 8}, {}));
}

INSTANTIATE_TEST_CASE_P(
    Filters,
    AccessRevocationManagerImplIsBlockedTest,
    testing::Combine(testing::Values(std::vector<uint8_t>{},
                                     std::vector<uint8_t>{7, 7, 7}),
                     testing::Values(std::vector<uint8_t>{},
                                     std::vector<uint8_t>{8, 8, 8})));

}  // namespace weave
