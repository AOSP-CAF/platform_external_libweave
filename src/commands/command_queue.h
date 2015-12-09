// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_SRC_COMMANDS_COMMAND_QUEUE_H_
#define LIBWEAVE_SRC_COMMANDS_COMMAND_QUEUE_H_

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <weave/device.h>

#include "src/commands/command_instance.h"

namespace weave {

class CommandQueue final {
 public:
  CommandQueue() = default;

  // TODO: Remove AddCommandAddedCallback and AddCommandRemovedCallback.
  using CommandCallback = base::Callback<void(Command* command)>;

  // Adds notifications callback for a new command is added to the queue.
  void AddCommandAddedCallback(const CommandCallback& callback);

  // Adds notifications callback for a command is removed from the queue.
  void AddCommandRemovedCallback(const CommandCallback& callback);

  void AddCommandHandler(const std::string& component_path,
                         const std::string& command_name,
                         const Device::CommandHandlerCallback& callback);

  // Checks if the command queue is empty.
  bool IsEmpty() const { return map_.empty(); }

  // Returns the number of commands in the queue.
  size_t GetCount() const { return map_.size(); }

  // Adds a new command to the queue. Each command in the queue has a unique
  // ID that identifies that command instance in this queue.
  // One shouldn't attempt to add a command with the same ID.
  void Add(std::unique_ptr<CommandInstance> instance);

  // Selects command identified by |id| ready for removal. Command will actually
  // be removed after some time.
  void DelayedRemove(const std::string& id);

  // Finds a command instance in the queue by the instance |id|. Returns
  // nullptr if the command with the given |id| is not found. The returned
  // pointer should not be persisted for a long period of time.
  CommandInstance* Find(const std::string& id) const;

 private:
  friend class CommandQueueTest;

  // Removes a command identified by |id| from the queue.
  bool Remove(const std::string& id);

  // Removes old commands selected with DelayedRemove.
  void Cleanup();

  // Overrides CommandQueue::Now() for tests.
  void SetNowForTest(base::Time now);

  // Returns current time.
  base::Time Now() const;

  // Overridden value to be returned from Now().
  base::Time test_now_;

  // ID-to-CommandInstance map.
  std::map<std::string, std::shared_ptr<CommandInstance>> map_;

  // Queue of commands to be removed.
  std::queue<std::pair<base::Time, std::string>> remove_queue_;

  using CallbackList = std::vector<CommandCallback>;
  CallbackList on_command_added_;
  CallbackList on_command_removed_;
  std::map<std::string, Device::CommandHandlerCallback> command_callbacks_;
  Device::CommandHandlerCallback default_command_callback_;

  DISALLOW_COPY_AND_ASSIGN(CommandQueue);
};

}  // namespace weave

#endif  // LIBWEAVE_SRC_COMMANDS_COMMAND_QUEUE_H_