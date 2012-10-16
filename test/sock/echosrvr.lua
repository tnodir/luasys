#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local ONE_SHOT_CLIENT = false
local DEBUG = false
local EXTRA_LOOPS = 0

local bind = {
  [8080] = "127.0.0.1",
}

local stderr = sys.stderr


-- Multi-threaded event queue?
local thread = sys.thread
local is_multithread = (EXTRA_LOOPS and EXTRA_LOOPS > 0)
if is_multithread then
  thread.init()
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

  chan_insert = function(evq, fd, cb, timeout)
    fd:nonblocking(true)
    if not evq:add_socket(fd, 'r', cb, timeout) then
      error(SYS_ERR)
    end

    if DEBUG then
      nskt = nskt + 1
      stderr:write("+ Insert in set (", nskt, ")\n")
    end
  end

  chan_remove = function(evq, evid, fd)
    evq:del(evid)
    fd:close()
    socket_put(fd)

    if DEBUG then
      nskt = nskt - 1
      stderr:write("- Remove from set (", nskt, ")\n")
    end
  end
end

local function process(evq, evid, fd, _, eof)
  local line
  if not eof then
    line = fd:read()
  end
  if line then
    line = fd:write(line)
    if ONE_SHOT_CLIENT then
      fd:shutdown()
    end
  end
  if not line then
    chan_remove(evq, evid, fd)
  end
end

local function accept(evq, evid, fd)
  local peer
  if DEBUG then
    peer = sock.addr()
  end
  local newfd = socket_get()

  if fd:accept(newfd, peer) then
    chan_insert(evq, newfd, process)

    if DEBUG then
      local port, addr = peer:inet()
      stderr:write("Peer: ", sock.inet_ntop(addr), ":", port, "\n")
    end
  else
    socket_put(newfd)
    stderr:write("accept: ", SYS_ERR, "\n")
  end
end


local evq = assert(sys.event_queue())

print("Binding servers...")
local saddr = sock.addr()
for port, host in pairs(bind) do
  local fd = sock.handle()
  assert(fd:socket())
  assert(fd:sockopt("reuseaddr", 1))
  assert(saddr:inet(port, sock.inet_pton(host)))
  assert(fd:bind(saddr))
  assert(fd:listen())
  assert(evq:add_socket(fd, 'accept', accept))
end

-- Quit by Ctrl-C
assert(evq:add_signal("INT", evq.stop))

print("Loop (+" .. (EXTRA_LOOPS or 0) .. ")...")
if is_multithread then
  for i = 1, EXTRA_LOOPS do
    assert(thread.run(evq.loop, evq))
  end
end
evq:loop()
