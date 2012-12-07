#!/usr/bin/env lua

local sys = require("sys")
local sock = require"sys.sock"


print"-- Directory Watch"
do
  local evq = assert(sys.event_queue())

  local filename = "test.tmp"
  local fd = sys.handle()

  local function on_change(evq, evid, path, ev)
    fd:close()
    sys.remove(filename)
    assert(ev == 'r', "file change notification expected")
  end

  sys.remove(filename)
  assert(evq:add_dirwatch(".", on_change, 100, true, false))
  assert(fd:create(filename))

  assert(evq:loop())
  print"OK"
end


print"-- Sockets Chain"
do
  local evq = assert(sys.event_queue())

  local pipe1, pipe2, pipe3

  local function read_cb(evq, evid, fd)
    fd:read(1)
    evq:del(evid)
    if pipe3 then
      pipe2[2]:write("e")
      evq:del(pipe3[3])
      pipe3[1]:close()
      pipe3[2]:close()
      pipe3 = nil
    end
  end

  local function create_socketpair()
    local pipe = {sock.handle(), sock.handle()}
    assert(pipe[1]:socket(pipe[2]))
    pipe[3] = assert(evq:add_socket(pipe[1], "r", read_cb))
    return pipe
  end

  pipe1 = create_socketpair()
  pipe2 = create_socketpair()
  pipe3 = create_socketpair()

  pipe1[2]:write("e")

  assert(evq:loop())
  print"OK"
end


print"-- Socket Pair"
do
  local evq = assert(sys.event_queue())

  local msg = "test"

  local function ev_cb(evq, evid, fd, ev)
    if ev == 'r' then
      local line = fd:recv()
      assert(line == msg, "Got: " .. tostring(line))
    elseif ev == 'w' then
      fd:send(msg)
    else
      error("Bad event: " .. ev)
    end
    evq:del(evid)
    fd:close()
  end

  local sd0, sd1 = sock.handle(), sock.handle()
  assert(sd0:socket(sd1))

  evq:add_socket(sd0, 'r', ev_cb)
  evq:add_socket(sd1, 'w', ev_cb)

  assert(evq:loop())
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

  assert(evq:loop())
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


-- WIN32
local win32 = sys.win32

if not win32 then return end


print"-- Registry Watch"
do
  local evq = assert(sys.event_queue())

  local keyname = "test.tmp"
  local hk = win32.registry()

  assert(hk:open("HKEY_CURRENT_USER",
    [[Software\Microsoft\Windows\CurrentVersion]], 'rw'))

  local function on_change(evq, evid, hk, ev)
    hk:del_value(keyname)
    hk:close()
    assert(ev == 'r', "registry key change notification expected")
  end

  hk:del_value(keyname)
  assert(evq:add_regwatch(hk, on_change, 100, true, false))
  assert(hk:set(keyname, 1234))

  assert(evq:loop())
  print"OK"
end


