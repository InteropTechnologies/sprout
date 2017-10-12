/**
 * @file thread_dispatcher_test.cpp UT for classes defined in
 *       thread_dispatcher.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "gtest/gtest.h"
#include "test_interposer.hpp"
#include "testingcommon.h"
#include "mock_load_monitor.h"
#include "mock_pjsip_module.h"
#include "siptest.hpp"
#include "stack.h"

#include "thread_dispatcher.h"

using ::testing::Return;
using ::testing::_;

class ThreadDispatcherTest : public SipTest
{
public:

  ThreadDispatcherTest()
  {
    mod_mock = new MockPJSipModule(stack_data.endpt,
                                   "test-module",
                                   PJSIP_MOD_PRIORITY_TRANSPORT_LAYER);
    init_thread_dispatcher(0, NULL, NULL, NULL, &load_monitor, NULL, 1000);
    mod_thread_dispatcher = get_mod_thread_dispatcher();

    cwtest_completely_control_time();
  }

  virtual void inject_msg_thread(TestingCommon::Message msg)
  {
    inject_msg_direct(msg.get_request(), mod_thread_dispatcher);
  }

  static void SetUpTestCase()
  {
    SipTest::SetUpTestCase();
  }

  static void TearDownTestCase()
  {
    SipTest::TearDownTestCase();
  }

  virtual ~ThreadDispatcherTest()
  {
    cwtest_reset_time();
    unregister_thread_dispatcher();
    delete mod_mock;
  }

  MockPJSipModule* mod_mock;
  MockLoadMonitor load_monitor;
  pjsip_module* mod_thread_dispatcher;
  pjsip_process_rdata_param rp;
};

TEST_F(ThreadDispatcherTest, NullTest)
{
  TestingCommon::Message msg;
  msg._first_hop = true;
  msg._method = "INVITE";
  msg._requri = "sip:bob@awaydomain";
  msg._from = "alice";
  msg._to = "bob";
  msg._todomain = "awaydomain";
  msg._route = "Route: <sip:proxy1.awaydomain;transport=TCP;lr>";

  EXPECT_CALL(load_monitor, admit_request(_)).WillOnce(Return(true));
  EXPECT_CALL(*mod_mock, on_rx_request(_)).WillOnce(Return(PJ_TRUE));
  EXPECT_CALL(load_monitor, request_complete(_));

  inject_msg_thread(msg);
  process_queue_element();
}

class SipEventQueueTest : public ::testing::Test
{
public:
  SipEventQueueTest()
  {
    // We can distinguish e1 and e2 by the value of en.event_data.rdata
    SipEventData event_data;
    event_data.rdata = &rdata_1;
    e1.type = MESSAGE;
    e1.event_data = event_data;

    event_data.rdata = &rdata_2;
    e2.type = MESSAGE;
    e2.event_data = event_data;

    PriorityEventQueueBackend* q_backend = new PriorityEventQueueBackend();
    q = new eventq<struct SipEvent>(0, true, q_backend);

    cwtest_completely_control_time();
  }

  virtual ~SipEventQueueTest()
  {
    cwtest_reset_time();

    delete q;
    q = nullptr;
  }

  SipEvent e1;
  SipEvent e2;

  pjsip_rx_data rdata_1;
  pjsip_rx_data rdata_2;

  eventq<struct SipEvent>* q;
};

// Test that higher priority SipEvents are 'larger' than lower priority ones.
TEST_F(SipEventQueueTest, PriorityOrdering)
{
  // Lower the priority of e2
  e2.priority = 1;

  // e1 should be 'larger' than e2
  EXPECT_TRUE(e1(e2, e1));
  EXPECT_TRUE(e2(e2, e1));
}

// Test that older SipEvents are 'larger' than newer ones at the same priority
// level.
TEST_F(SipEventQueueTest, TimeOrdering)
{
  // Set e1 to be older than e2
  e1.stop_watch.start();
  cwtest_advance_time_ms(1);
  e2.stop_watch.start();

  // e1 should be 'larger' than e2
  EXPECT_TRUE(e1(e2, e1));
  EXPECT_TRUE(e2(e2, e1));
}

// Test that SipEvents are ordered by priority before time.
TEST_F(SipEventQueueTest, PriorityAndTimeOrdering)
{
  // Lower the priority of e2
  e2.priority = 1;

  // Set e2 to be older than e1
  e1.stop_watch.start();
  cwtest_advance_time_ms(1);
  e2.stop_watch.start();

  // e1 should be 'larger' than e2
  EXPECT_TRUE(e1(e2, e1));
  EXPECT_TRUE(e2(e2, e1));
}

// Test that higher priority SipEvents are returned before lower priority ones.
TEST_F(SipEventQueueTest, QueuePriorityOrdering)
{
  // Lower the priority of e2
  e2.priority = 1;

  q->push(e2);
  q->push(e1);

  SipEvent e;

  // e1 is higher priority, so should be returned first
  q->pop(e);
  EXPECT_EQ(e1.event_data.rdata, e.event_data.rdata);

  q->pop(e);
  EXPECT_EQ(e2.priority, e.priority);
}

// Test that older SipEvents are returned before newer ones at the same priority
// level.
TEST_F(SipEventQueueTest, QueueTimeOrdering)
{
  // Set e1 to be older than e2
  e1.stop_watch.start();
  cwtest_advance_time_ms(1);
  e2.stop_watch.start();

  q->push(e2);
  q->push(e1);

  SipEvent e;

  // e1 is older, so should be returned first
  q->pop(e);
  EXPECT_EQ(e1.event_data.rdata, e.event_data.rdata);

  q->pop(e);
  EXPECT_EQ(e2.event_data.rdata, e.event_data.rdata);
}

// Test that SipEvents are returned from the queue in priority, then time, order
TEST_F(SipEventQueueTest, QueuePriorityAndTimeOrdering)
{
  // Lower the priority of e2
  e2.priority = 1;

  // Set e2 to be older than e1
  e1.stop_watch.start();
  cwtest_advance_time_ms(1);
  e2.stop_watch.start();

  q->push(e2);
  q->push(e1);

  SipEvent e;

  // e1 is higher priority, so should be returned first despite e2 being older
  q->pop(e);
  EXPECT_EQ(e1.event_data.rdata, e.event_data.rdata);

  q->pop(e);
  EXPECT_EQ(e2.event_data.rdata, e.event_data.rdata);
}

// TODO: Test overloading
