#!/usr/bin/env lua

local sys = require"sys"


local evq = assert(sys.event_queue())

-- Timer
local timer_id
do
  local function on_timer()
    print"Working..."
  end

  timer_id = assert(evq:add_timer(on_timer, 500))
end

-- Subprocess
do
  local pid = sys.pid()
  local fdi, fdo = sys.handle(), sys.handle()
  assert(fdi:pipe(fdo))
  do
    local sleep_msec = 1000
    local subprocess = [[
      sys.thread.sleep(]] .. sleep_msec .. [[)
      sys.stdout:write"Exited normally."
    ]]
    print("Subprocess sleep:", sleep_msec)

    local args = {"-l", "sys", "-e", subprocess}
    if not sys.spawn("lua", args, pid, nil, fdo, nil) then
      error(SYS_ERR)
    end
    fdo:close()
  end

  local function on_child(evq, evid, pid, ev, err)
    evq:del(timer_id)
    if ev == 't' then
      print("Timeout: Kill the subprocess")
      assert(pid:kill())
      assert(evq:add_pid(pid, on_child))
    else
      print("Status:", err or 0)
      if err and err ~= 0  then
        print("Subprocess killed.")
      else
        print("Subprocess output:", fdi:read())
      end
    end
  end

  assert(evq:add_pid(pid, on_child, 1014))
end

evq:loop()

