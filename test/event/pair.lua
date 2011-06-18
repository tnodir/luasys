#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local TIMEOUT = 2000  -- milliseconds

local sd0, sd1 = sock.handle(), sock.handle()
assert(sd0:socket(sd1))

local function ev_cb(evq, evid, fd, R, W, T, EOF)
    print(fd, R and "Read" or "", W and "Write" or "",
	T and "Timeout" or "", EOF and "EOF" or "")

    if R then
	local line = fd:recv()
	sys.stdout:write("Output:\t", line)
    end
    if W then
	sys.stdout:write"Input:\t"
	local line = sys.stdin:read()

	fd:send(line)
    end
    evq:del(evid)
    fd:close()
end

local evq = assert(sys.event_queue())

evq:add_socket(sd0, 'r', ev_cb, TIMEOUT)
evq:add_socket(sd1, 'w', ev_cb, TIMEOUT)

evq:loop()
