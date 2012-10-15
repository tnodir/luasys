#!/usr/bin/env lua

local coroutine = require("coroutine")

local sys = require("sys")
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


-- Scheduler
local sched, sched_stop
do
  local function controller(co)
    if co and sched_stop and sched:size() == 0 then
      sched:stop()
    end
  end

  sched = assert(thread.scheduler(controller))

  -- Workers
  assert(thread.run(sched.loop, sched))
end


print("-- Add coroutine as task")
do
  local msg = "test"

  local function test(s)
    assert(s == msg, msg .. " expected, got " .. tostring(s))
  end

  local co = assert(coroutine.create(test))
  local task = assert(sched:put(co, msg))
end


print("-- Suspend/Resume")
do
  local msg = "test"

  local function test()
    local s = sched:suspend()
    assert(s == msg, msg .. " expected, got " .. tostring(s))
  end

  local task = assert(sched:put(test))

  thread.sleep(10)
  sched:resume(task, msg)
end


print("-- Preemptive multi-tasking")
do
  assert(thread.run(sched.preempt_tasks, sched, 50))
  thread.sleep(10)

  local condition

  local function test1()
    local i = 0
    while not condition do
      i = i + 1
    end
  end

  local function test2()
    coroutine.yield()
    condition = true
  end

  assert(sched:put(test1))
  assert(sched:put(test2))
end


sched_stop = true

assert(sched:loop())
print("OK")

