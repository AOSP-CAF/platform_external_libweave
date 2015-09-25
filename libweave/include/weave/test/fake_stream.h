// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_INCLUDE_WEAVE_TEST_FAKE_STREAM_H_
#define LIBWEAVE_INCLUDE_WEAVE_TEST_FAKE_STREAM_H_

#include <weave/stream.h>

#include <string>

#include <base/time/time.h>
#include <gmock/gmock.h>

namespace weave {

class TaskRunner;

namespace test {

class FakeStream : public Stream {
 public:
  explicit FakeStream(TaskRunner* task_runner);
  FakeStream(TaskRunner* task_runner, const std::string& read_data);

  void ExpectWritePacketString(base::TimeDelta, const std::string& data);
  void AddReadPacketString(base::TimeDelta, const std::string& data);

  void CancelPendingOperations() override;
  void Read(void* buffer,
            size_t size_to_read,
            const ReadSuccessCallback& success_callback,
            const ErrorCallback& error_callback) override;
  void Write(const void* buffer,
             size_t size_to_write,
             const SuccessCallback& success_callback,
             const ErrorCallback& error_callback) override;

 private:
  TaskRunner* task_runner_{nullptr};
  std::string write_data_;
  std::string read_data_;
};

}  // namespace test
}  // namespace weave

#endif  // LIBWEAVE_INCLUDE_WEAVE_TEST_FAKE_STREAM_H_
