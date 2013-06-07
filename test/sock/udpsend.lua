#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local port = 1313
local host = "127.0.0.1"

local fd = sock.handle()
assert(fd:socket("dgram"))

local saddr = assert(sock.addr())
assert(saddr:inet(port, sock.inet_pton(host)))

assert(fd:connect(saddr))

assert(fd:send(sys.stdin:read(13)))
