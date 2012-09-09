#!/usr/bin/env lua

local sys = require("sys")
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


local evq = assert(sys.event_queue())

-- Scheduler
local sched, sched_stop
do
  local function controller(co)
    if co and sched_stop and sched:size() == 0 then
      sched:stop()
      evq:stop()
    end
  end

  sched = assert(thread.scheduler(controller))

  -- Workers
  assert(thread.run(sched.loop, sched))
end


print("-- Timer event")
do
  local timeout = 100

  local function yield_timeout()
    local ev = sched:wait_timer(evq, timeout)
    assert(ev == 't', timeout .. " msec timeout expected")
  end

  assert(sched:put(yield_timeout))
end


print("-- Socket event")
do
  local msg = "test"

  local function on_read(fd)
    local ev = sched:wait_socket(evq, fd, "r")
    assert(ev == 'r')
    local s = assert(fd:read())
    assert(s == msg, msg .. " expected, got " .. tostring(s))
  end

  local fdi, fdo = sock.handle(), sock.handle()
  assert(fdi:socket(fdo))

  assert(sched:put(on_read, fdi))

  thread.sleep(100)
  assert(fdo:write(msg))
end


sched_stop = true

assert(evq:loop(nil, true))
print("OK")

