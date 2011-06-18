#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local host, port = "127.0.0.1", 8080

local stdin, stdout = sys.stdin, sys.stdout

local fd = sock.handle()
assert(fd:socket())

assert(fd:connect(sock.addr():inet(port, sock.inet_pton(host))))

while true do
    local line = stdin:read()
    if not fd:write(line) then break end
    line = fd:read()
    if not line then break end
    stdout:write(line)
end
