#!/usr/bin/env lua

local io = require("io")
local sys = require("sys")
local sock = require("sys.sock")


print"-- file:isatty()"
do
  assert(sys.stdin:isatty())
  print("OK")
end


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
  assert(log:warn("Warning"))
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
    sys.stdout:write(i, "\taddress family: ", af, "\n")
    if af then
      if iface.addr then
        sys.stdout:write("\tinet addr: ", sock.inet_ntop(iface.addr),
          " <", sock.getnameinfo(iface.addr), ">",
          " Mask: ", sock.inet_ntop(iface.netmask), "\n")
      end
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
  for file, type in sys.dir('.') do
    print(file, type and "DIR" or "FILE")
  end
  print("OK")
end


print"-- sys.date & sys.time"
do
  local format = "%d.%m.%Y %H:%M:%S"
  local now = assert(sys.time())
  local date = assert(sys.date(format))
  local time = assert(sys.time(date, format))
  assert(now == time)
  print("OK")
end


print"-- sys.loadavg"
do
  local nprocs = sys.nprocs()
  for i = 1, 5 do
    local loadavg, is_per_cpu = assert(sys.loadavg())
    if is_per_cpu then
      loadavg = loadavg / nprocs
    end
    print("loadavg:", loadavg)
    sys.thread.sleep(100)
  end
  print("OK")
end


