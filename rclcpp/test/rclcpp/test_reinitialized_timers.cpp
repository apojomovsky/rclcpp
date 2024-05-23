// Copyright 2024 iRobot Corporation. All Rights Reserved.

#include <gtest/gtest.h>

#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <rclcpp/experimental/executors/events_executor/lock_free_events_queue.hpp>
#include <rclcpp/rclcpp.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using rclcpp::experimental::executors::EventsExecutor;

static const size_t timeout_seconds{2};

class TimersTest
: public testing::Test
{
public:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
  }

  void TearDown() override
  {
    rclcpp::shutdown();
  }

  void run_timers_test(
    std::unique_ptr<EventsExecutor> &executor,
    std::shared_ptr<rclcpp::Node> &node,
    std::chrono::milliseconds timers_period,
    size_t &count_1,
    size_t &count_2,
    std::atomic<bool> &stop_signal)
  {
    auto timer_1 = rclcpp::create_timer(
      node,
      node->get_clock(),
      rclcpp::Duration(timers_period),
      [&count_1]()
      {
        count_1++;
      });

    auto timer_2 = rclcpp::create_timer(
      node,
      node->get_clock(),
      rclcpp::Duration(timers_period),
      [&count_2]()
      {
        count_2++;
      });

    std::thread executor_thread([&executor, &stop_signal]()
    {
      while (!stop_signal.load() && rclcpp::ok())
      {
        executor->spin_all(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

    while (count_2 < 10u && !stop_signal.load())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stop_signal.store(true);
    executor->cancel();
    executor_thread.join();

    EXPECT_GE(count_2, 10u);
    EXPECT_LE(count_2 - count_1, 1u);
  }
};

TEST_F(TimersTest, TimersWithSamePeriod)
{
  auto timers_period = std::chrono::milliseconds(5);
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto events_queue = std::make_unique<rclcpp::experimental::executors::LockFreeEventsQueue>();
  auto executor = std::make_unique<EventsExecutor>(
    std::move(events_queue),
    true,
    rclcpp::ExecutorOptions());

  executor->add_node(node);

  std::promise<void> promise;
  std::future<void> future = promise.get_future();
  std::atomic<bool> stop_signal(false);
  std::thread test_thread([this, &promise, &executor, &node, timers_period, &stop_signal]()
  {
    size_t count_1 = 0;
    size_t count_2 = 0;

    run_timers_test(executor, node, timers_period, count_1, count_2, stop_signal);

    count_1 = 0;
    count_2 = 0;
    stop_signal.store(false);
    run_timers_test(executor, node, timers_period, count_1, count_2, stop_signal);

    promise.set_value();
  });

  if (future.wait_for(std::chrono::seconds(timeout_seconds)) == std::future_status::timeout)
  {
    stop_signal.store(true);
    executor->cancel();
    test_thread.join();
    FAIL() << "Test timed out";
  }
  else
  {
    test_thread.join();
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
