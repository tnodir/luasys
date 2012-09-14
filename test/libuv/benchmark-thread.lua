#!/usr/bin/env lua

local sys = require("sys")

local thread = sys.thread
thread.init()


local NUM_THREADS = 20 * 1000

local num_threads = 0


local function thread_entry(arg)
  assert(arg == 42)
  num_threads = num_threads + 1
end


local function thread_create()
  local period = sys.period()

  period:start()

  for i = 1, NUM_THREADS do
    local tid = thread.run(thread_entry, 42)
    if not tid then
      error(SYS_ERR)
    end

    if not tid:wait() then
      error(SYS_ERR)
    end
  end

  local duration = period:get() / 1e6

  assert(num_threads == NUM_THREADS)

  print(NUM_THREADS .. " threads created in " .. duration
      .. " seconds (" .. (NUM_THREADS / duration) .. "/s)")

  return 0
end

return thread_create()
