#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local path = "/tmp/test_sock_lua"

local fd = sock.handle()
assert(fd:socket("dgram", "unix"))

local saddr = assert(sock.addr():file(path))
assert(fd:connect(saddr))

fd:send(sys.stdin:read(13))
