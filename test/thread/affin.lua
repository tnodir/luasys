#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()

print"<Enter> to start (You may set CPU affinity for process)..."
sys.stdin:read()

local COUNT = 1000000
local NPROCS = thread.nprocs() or sys.nprocs()

if NPROCS > COUNT then
  NPROCS = COUNT
end

local period = sys.period()
period:start()

-- Pipe
local work_pipe = thread.pipe()

-- Worker
local function process(work_pipe, from, count)
  local sum = 0
  for i = from, from + count do
    sum = sum + i
  end
  work_pipe:put(sum)
end

-- Start workers
do
  local step = COUNT / NPROCS
  local func = string.dump(process)

  for i = 1, NPROCS do
    assert(thread.runvm({cpu = i}, func, work_pipe,
      (i - 1) * step + 1, step - 1))
  end
end

-- Wait result
do
  local sum = 0

  for i = 1, NPROCS do
    local _, res = work_pipe:get()
    sum = sum + res
  end

  print("time:", period:get() / 1000)
  print("nprocs:", NPROCS)
  print("count:", COUNT)
  print("sum:", sum)
end
