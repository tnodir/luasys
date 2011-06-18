#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local host, port = "127.0.0.1", 8080
local nclnt = 2000

local stdin, stdout, stderr = sys.stdin, sys.stdout, sys.stderr


local askt, iskt = {}, 0

local function ev_cb(evq, evid, fd, R)
    local s = tostring(fd)
    if not R then
	assert(fd:write(s))
	assert(evq:mod_socket(evid, 'r'))
    else
	local line = fd:read()
	if line ~= s then
	    error("got: " .. tostring(line)
		.. " expected: " .. s)
	end

	iskt = iskt + 1
	if nclnt - iskt < 1 then
	    -- close all sockets
	    for i = 1, nclnt do
		evid = askt[i]
		evq:del(evid)
		fd:close()
	    end
	end
    end
end


local start_time = sys.msec()

local evq = assert(sys.event_queue())

local saddr = sock.addr():inet(port, sock.inet_pton(host))
for i = 1, nclnt do
    local fd = sock.handle()
    assert(fd:socket())
    assert(fd:connect(saddr))

    local evid = evq:add_socket(fd, 'w', ev_cb)
    if not evid then
	error(errorMessage)
    end
    askt[i] = evid
end


stdout:write(nclnt, " sessions opened in ", sys.msec() - start_time,
    " msec\nPress any key to send data...\n")
stdin:read()

local start_time = sys.msec()

evq:loop()

stdout:write("Sessions closed in ", sys.msec() - start_time, " msec\n")
