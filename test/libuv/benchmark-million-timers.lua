#!/usr/bin/env lua

local sys = require("sys")


local NUM_TIMERS = 1000 * 1000

local timer_cb_called = 0


local function timer_cb(evq)
  timer_cb_called = timer_cb_called + 1
end


local function million_timers()
  local evq = assert(sys.event_queue())
  local period = sys.period()

  local timeout = 0
  for i = 1, NUM_TIMERS do
    if (i % 1000 == 0) then timeout = timeout + 1 end
    if not evq:add_timer(timer_cb, timeout, true) then
      error(SYS_ERR)
    end
  end

  period:start()
  assert(evq:loop())
  local duration = period:get() / 1e6

  assert(timer_cb_called == NUM_TIMERS)

  print(duration .. " seconds")

  return 0
end


return million_timers()
