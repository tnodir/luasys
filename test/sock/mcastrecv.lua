#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local MCAST_ADDR = "234.5.6.7"
local MCAST_PORT = 25000

local fd = sock.handle()
assert(fd:socket("dgram"))

assert(fd:sockopt("reuseaddr", 1))
assert(fd:bind(sock.addr():inet(MCAST_PORT)))

assert(fd:membership(sock.inet_pton(MCAST_ADDR)))

while true do
	sys.stdout:write(fd:recv())
end
