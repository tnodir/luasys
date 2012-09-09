#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


local ONE_SHOT_CLIENT = false
local DEBUG = false

local bind = {
  [8080] = "127.0.0.1",
}

local stderr = sys.stderr


local evq = assert(sys.event_queue())
local sched = assert(thread.scheduler())


-- Process scheduler in main thread
do
  local function on_idle()
    sched:loop(0)
  end

  evq:on_idle(on_idle)
end


-- Pool of sockets
local socket_get, socket_put
do
  local pool = setmetatable({n = 0}, {__mode = "v"})

  socket_get = function()
    local n = pool.n
    local fd = pool[n]
    if not fd then
      fd = sock.handle()
      n = 1
    end
    pool.n = n - 1
    return fd
  end

  socket_put = function(fd)
    local n = pool.n + 1
    pool.n, pool[n] = n, fd
  end
end


-- Channels
local chan_insert, chan_remove
do
  local nskt = 0

  chan_insert = function(fd, task_fn)
    fd:nonblocking(true)
    if not sched:put(task_fn, fd) then
      error(SYS_ERR)
    end

    if DEBUG then
      nskt = nskt + 1
      stderr:write("+ Insert in set (", nskt, ")\n")
    end
  end

  chan_remove = function(fd)
    fd:close()
    socket_put(fd)

    if DEBUG then
      nskt = nskt - 1
      stderr:write("- Remove from set (", nskt, ")\n")
    end
  end
end

local function process(fd, R, W, T, eof)
  local line
  if not eof then
    line = fd:read()
  end
  if line then
--print("write", fd, line)
    line = fd:write(line)
    if ONE_SHOT_CLIENT then
      fd:shutdown()
    end
  end
  if not line then
    chan_remove(fd)
    return false
  end
  return true
end

local function task_process(fd)
  while process(fd, sched:wait_socket(evq, fd, 'r')) do end
end

local function accept(_, _, fd)
  local peer
  if DEBUG then
    peer = sock.addr()
  end
  local newfd = socket_get()

  if fd:accept(newfd, peer) then
    chan_insert(newfd, task_process)

    if DEBUG then
      local port, addr = peer:inet()
      stderr:write("Peer: ", sock.inet_ntop(addr), ":", port, "\n")
    end
  else
    stderr:write("accept: ", SYS_ERR, "\n")
  end
end

local function task_accept(fd)
  while true do
    if not sched:wait_socket(evq, fd, 'accept') then
      error(SYS_ERR)
    end

    accept(nil, nil, fd)
  end
end


print("Binding servers...")
do
  local saddr = sock.addr()
  for port, host in pairs(bind) do
    local fd = sock.handle()
    assert(fd:socket())
    assert(fd:sockopt("reuseaddr", 1))
    assert(saddr:inet(port, sock.inet_pton(host)))
    assert(fd:bind(saddr))
    assert(fd:listen())
    assert(fd:nonblocking(true))
    --assert(sched:put(task_accept, fd))
    assert(evq:add_socket(fd, 'accept', accept))
  end
end

-- Quit by Ctrl-C
assert(evq:add_signal("INT", evq.stop))

print("Loop...")
evq:loop()
