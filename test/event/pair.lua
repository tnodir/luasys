#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local TIMEOUT = 2000  -- milliseconds

local sd0, sd1 = sock.handle(), sock.handle()
assert(sd0:socket(sd1))

local function ev_cb(evq, evid, fd, ev)
  if ev == 'r' then
    local line = fd:recv()
    sys.stdout:write("Output:\t", line)
  elseif ev == 'w' then
    sys.stdout:write"Input:\t"
    fd:send(sys.stdin:read())
  end
  evq:del(evid)
  fd:close()
end

local evq = assert(sys.event_queue())

evq:add_socket(sd0, 'r', ev_cb, TIMEOUT)
evq:add_socket(sd1, 'w', ev_cb, TIMEOUT)

evq:loop()
