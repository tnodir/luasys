#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local host, port = "127.0.0.1", 8080

local stdin, stdout = sys.stdin, sys.stdout

local fd = sock.handle()
assert(fd:socket())
assert(fd:nonblocking(true))

local addr = assert(sock.getaddrinfo(host))

local res, err = fd:connect(sock.addr():inet(port, addr))
if res == nil then error(err) end

if not res then
    local function on_connect(evq, evid, fd, R, W, timeout)
	if timeout then error"Timeout" end

	local errno = fd:sockopt("error")
	if errno ~= 0 then
	    error(sys.strerror(errno))
	end
    end

    local evq = assert(sys.event_queue())

    assert(evq:add_socket(fd, "connect", on_connect, 5000))

    evq:loop()
end

print("OK")
