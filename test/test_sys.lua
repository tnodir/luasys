#!/usr/bin/env lua

local sys = require("sys")
local sock = require("sys.sock")


print"-- sys.handle <-> io.file"
do
    local file = assert(io.open("test", "w"))
    local fd = sys.handle()
    fd:fileno(file)
    assert(fd:write"fd <")
    assert(fd:fdopen(file, "w"))
    assert(file:write"> file")
    file:flush()
    file:close()
    sys.remove"test"
    print("OK")
end


print"-- Logs"
do
    local log = assert(sys.log())
    assert(log:error("Error"):warn("Warning"))
    print("OK")
end


print"-- Random"
do
    local rand = assert(sys.random())
    for i = 1, 20 do
	sys.stdout:write(rand(10), "; ")
    end
    print("\nOK")
end


print"-- Emulate popen()"
do
    local fdi, fdo = sys.handle(), sys.handle()
    assert(fdi:pipe(fdo))
    local s = "test pipe"
    assert(sys.spawn("lua",
	{'-l', 'sys', '-e', 'sys.stdout:write[[' .. s .. ']]'},
	nil, nil, fdo))
    fdo:close()
    assert(fdi:read() == s)
    fdi:close()
    print("OK")
end


print"-- SocketPair"
do
    local fdi, fdo = sock.handle(), sock.handle()
    assert(fdi:socket(fdo))
    local s = "test socketpair"
    assert(fdo:write(s))
    fdo:close()
    assert(fdi:read() == s)
    fdi:close()
    print("OK")
end


print"-- Interface List"
do
    local ifaddrs = assert(sock.getifaddrs())
    for i, iface in ipairs(ifaddrs) do
	local af = iface.family
	sys.stdout:write(i, "\taddress family: ", af or "unknown", "\n")
	if af then
	    local host = sock.getnameinfo(iface.addr)
	    sys.stdout:write("\tinet addr: ", sock.inet_ntop(iface.addr),
		" <", host, ">", " Mask: ", sock.inet_ntop(iface.netmask), "\n")
	    local flags = iface.flags
	    sys.stdout:write("\t",
		flags.up and "UP " or "",
		flags.loopback and "LOOPBACK " or "",
		flags.pointtopoint and "POINTOPOINT " or "",
		"\n")
	end
    end
    print("OK")
end


print"-- Directory List"
do
    for file, type in sys.dir('/') do
	print(file, type and "DIR" or "FILE")
    end
    print("OK")
end


