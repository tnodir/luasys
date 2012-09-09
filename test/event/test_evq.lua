#!/usr/bin/env lua

local sys = require("sys")


print"-- Directory Watch"
do
  local evq = assert(sys.event_queue())

  local filename = "test.txt"
  local fd = sys.handle()

  local function on_change(evq, evid, path, ev)
    fd:close()
    sys.remove(filename)
    assert(ev ~= 't', "file change notification expected")
  end

  assert(evq:add_dirwatch(".", on_change, 100, true))
  assert(fd:create(filename))

  evq:loop()
  print"OK"
end


print"-- Coroutines"
do
  local evq = assert(sys.event_queue())

  local function sleep(msg)
    print(msg, coroutine.yield())
  end

  local function start(co, num)
    local evid = assert(evq:add_timer(co, 10))
    sleep"init"
    assert(evq:timeout(evid, 20))
    sleep"work"
    assert(evq:timeout(evid, 30))
    sleep"done"
    if num % 2 == 0 then
      assert(evq:del(evid))
    end
  end

  for i = 1, 3 do
    local co = assert(coroutine.create(start))
    assert(coroutine.resume(co, co, i))
  end

  evq:loop()
  print"OK"
end


print"-- Signal: wait SIGINT"
do
  local function on_signal(evq, evid, _, ev)
    if ev == 't' then
      assert(evq:timeout(evid))
      assert(evq:ignore_signal("INT", false))
      print"SIGINT enabled. Please, press Ctrl-C..."
    else
      assert(evq:del(evid))
      print"Thanks!"
    end
  end

  local evq = assert(sys.event_queue())

  assert(evq:add_signal("INT", on_signal, 3000))
  assert(evq:ignore_signal("INT", true))

  evq:loop(30000)
  print"OK"
end


