#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


print"-- Thread:Coroutine interruption"
do
  local function work()
    while true do
      thread.sleep(5)
    end
  end

  local function test()
    local co = assert(coroutine.create(work))
    local _, err = coroutine.resume(co)
    if err and not (thread.self():interrupted()
        and err == thread.interrupt_error()) then
      print("Error:", err)
      err = nil
    end
    if not err then
      error("Thread Interrupt Error expected")
    end
  end

  local td = assert(thread.run(test))

  thread.switch()
  td:interrupt()
  assert(td:wait() == 0)
  print"OK"
end


assert(thread.self():wait())
