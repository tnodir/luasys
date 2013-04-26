#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


-- Data Pool
local dpool
do
  local function on_full(dpool, ...)
    print("put on full>", ...)
    return ...
  end

  local function on_empty()
    print("get on empty>")
  end

  dpool = assert(thread.data_pool())
  dpool:callbacks(on_full, on_empty)
  dpool:max(2)  -- set maximum watermark
end

-- Consumer Thread
local consumer
do
  local function consume()
    while true do
      local i, s = dpool:get(200)
      if not i then break end
      print(i, s)
      thread.sleep(200)
    end
  end

  consumer = assert(thread.run(consume))
end

-- Producer Thread
local producer
do
  local function produce()
    for i = 1, 10 do
      dpool:put(i, (i % 2 == 0) and "even" or "odd")
      thread.sleep(100)
    end
  end

  producer = assert(thread.run(produce))
end

-- Wait threads termination
assert(consumer:wait())
assert(producer:wait())
