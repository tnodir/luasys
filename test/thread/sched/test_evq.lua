#!/usr/bin/env lua

local sys = require("sys")
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


local evq = assert(sys.event_queue())

-- Scheduler
local sched
do
  sched = assert(thread.scheduler())

  -- Workers
  assert(thread.run(sched.loop, sched))
end


print("-- Timer event")
local task
do
  local timeout = 10

  local function on_timeout()
    local ev = sched:wait_timer(evq, timeout)
    assert(ev == 't', timeout .. " msec timeout expected")
  end

  assert(sched:put(on_timeout))
end


print("-- Socket event")
do
  local msg = "test"

  local fdi, fdo = sock.handle(), sock.handle()
  assert(fdi:socket(fdo))

  local function on_read()
    local ev = sched:wait_socket(evq, fdi, "r")
    assert(ev == 'r')
    local s = assert(fdi:read())
    assert(s == msg, msg .. " expected, got " .. tostring(s))
    fdi:close()
    fdo:close()
  end

  assert(sched:put(on_read))

  thread.sleep(100)
  assert(fdo:write(msg))
end


print("-- Terminate task, waiting on event")
do
  local timeout = 100

  local fdi, fdo = sock.handle(), sock.handle()
  assert(fdi:socket(fdo))

  local function on_timeout(task)
    local ev = sched:wait_timer(evq, timeout)
    assert(ev == 't', timeout .. " msec timeout expected")
    assert(sched:terminate(task))
    fdi:close()
    fdo:close()
  end

  local function on_read(fd)
    sched:wait_socket(evq, fd, "r")
    assert(false, "Termination expected")
  end

  local task = assert(sched:put(on_read, fdi))
  assert(sched:put(on_timeout, task))

  thread.sleep(100)
end


assert(evq:loop())
print("OK")

sched:stop()
