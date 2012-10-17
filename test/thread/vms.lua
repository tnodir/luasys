#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


local COUNT = 10000

local period = sys.period()
period:start()

-- Pipe
local work_pipe = thread.pipe()

-- Producer VM-Thread
do
  local function produce(work_pipe, from, to)
    local sys = require"sys"

    local rep = string.rep
    local thread = sys.thread
    local rand = assert(sys.random())

    for i = from, to do
      local d = i % 10
      local r = rand(460)
      local data = rep(d, r + 1)
      work_pipe:put(i, r, data)
    end
  end

  local func = string.dump(produce)
  assert(thread.runvm(nil, func, work_pipe, 1, COUNT/2))
  assert(thread.runvm(nil, func, work_pipe, COUNT/2 + 1, COUNT))
end

-- Consumer VM-Thread
do
  local function consume(work_pipe, from, to)
    local sys = require"sys"

    local rep = string.rep
    local thread = sys.thread

    local expect = to - from + 1
    local count = 0
    for i = from, to do
      local _, num, r, s = work_pipe:get()
      if num == false then break end
      local d = num % 10
      local data = rep(d, r + 1)
      if s ~= data then
        error(data .. " string expected, got " .. s)
      end
      count = count + 1
    end
    assert(count == expect, expect .. " messages expected, got " .. count)
  end

  local func = string.dump(consume)
  assert(thread.runvm(nil, func, work_pipe, 1, COUNT/2))
  assert(thread.runvm(nil, func, work_pipe, COUNT/2 + 1, COUNT))
end

-- Wait VM-Threads termination
assert(thread.self():wait())

print("time:", period:get() / 1000)
print("count:", COUNT)
