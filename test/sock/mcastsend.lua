#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local MCAST_ADDR = "234.5.6.7"
local MCAST_PORT = 25000

local fd = sock.handle()
assert(fd:socket("dgram"))

assert(fd:sockopt("reuseaddr", 1))

local saddr = sock.addr():inet(MCAST_PORT + 1)
assert(fd:bind(saddr))

sys.stdout:write("Enter text: ")
local line = sys.stdin:read()

saddr:inet(MCAST_PORT, sock.inet_pton(MCAST_ADDR))
assert(fd:send(line, saddr))
