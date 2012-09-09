
local sys = require("sys")

local win32 = assert(sys.win32, "Windows 9x/NT required")


print"-- Mailslot"
do
  local fd = sys.handle()
  assert(fd:mailslot([[\\.\mailslot\luasys]]))
  print(fd:get_mailslot_info())
  fd:close()
  print"OK"
end


print"-- Registry"
do
  local r = win32.registry()
  assert(r:open("HKEY_LOCAL_MACHINE",
    [[Software\Microsoft\Windows\CurrentVersion\Setup]]))
  for i, k, v in r:values() do
    sys.stdout:write(k, ' = "', v, '"\n')
  end
  if r:set("TEST_SET", 666) then
    error("'Access denied.' expected")
  end
  r:close()
  print("OK")
end


