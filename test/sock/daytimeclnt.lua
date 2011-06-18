#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local host, port = "127.0.0.1", 13
local MAX_MSG_LEN = 100

local fd = sock.handle()
assert(fd:socket("dgram"))

local saddr = sock.addr()
assert(saddr:inet(port, sock.inet_pton(host)))

fd:nonblocking(true)
if fd:send("get time", saddr) then
    local data = assert(fd:recv(MAX_MSG_LEN, saddr))
    port, host = saddr:inet()
    print(sock.inet_ntop(host) .. ":" .. port .. "> " .. data)
end
