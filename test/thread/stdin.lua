#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


-- Usage notes
print[[
Press <Enter> to quit or any chars for feedback...
]]


local stdin = sys.stdin

-- Event Queue
local evq = assert(sys.event_queue())

local worker

-- Controller
local controller
do
  local function on_event(evq, evid)
    print("Controller:", "Close stdin")
    worker:interrupt()
    stdin:close(true)  -- Win32 workaround
    assert(worker:wait() == -1)
    evq:del(evid)
  end

  controller = assert(evq:add_timer(on_event, 30000))
end

-- Worker Thread
do
  local function read_stdin()
    while true do
      local line = stdin:read()
      if #line <= 2 then
        print("Worker:", "Notify controller")
        evq:sync(evq.notify, evq, controller)
      else
        sys.stdout:write("Worker:\tInput: ", line)
      end
    end
  end

  local function start()
    local _, err = pcall(read_stdin)
    if err and not (thread.self():interrupted()
        and err == thread.interrupt_error()) then
      print("Error:", err)
      err = nil
    end
    if not err then
      error("Thread Interrupt Error expected")
    end
    print("Worker:", "Terminated")
    return -1
  end

  worker = assert(thread.run(start))
end

assert(evq:loop())
