#!/usr/bin/env lua

-- libevent/test/bench_cascade.c

--[[
 * This benchmark tests how quickly we can propagate a write down a chain
 * of socket pairs.  We start by writing to the first socket pair and all
 * events will fire subsequently until the last socket pair has been reached
 * and the benchmark terminates.
--]]


local sys = require"sys"
local sock = require"sys.sock"


local num_pipes, fired

local pipes, events, events_idx

local period = sys.period()


local function read_cb(evq, evid, fd)
  fd:read(1)
  local idx = events_idx[evid]
  if idx < num_pipes then
    pipes[idx + 1][2]:write("e")
  end
  fired = fired + 1
end

local function run_once(evq)
  pipes, events, events_idx = {}, {}, {}

  for i = 1, num_pipes do
    local fdi, fdo = sock.handle(), sock.handle()
    if not fdi:socket(fdo) then
      error(SYS_ERR)
    end
    pipes[i] = {fdi, fdo}
  end

  -- measurements includes event setup
  period:start()

  -- provide a default timeout for events
  local timeout = 60000

  for i = 1, num_pipes do
    local fdi = pipes[i][1]
    evid = evq:add_socket(fdi, "r", read_cb, timeout)
    if not evid then
      error(SYS_ERR)
    end
    events[i], events_idx[evid] = evid, i
  end

  fired = 0;

  -- kick everything off with a single write
  pipes[1][2]:write("e")

  local xcount = 0
  while true do
    evq:loop(0)
    if fired < num_pipes then
      xcount = xcount + 1
    else break end
  end
  if xcount ~= 0 then
    sys.stderr:write("Xcount: ", xcount, "\n")
  end

  local res = period:get()

  for i = 1, num_pipes do
    evq:del(events[i])
    local pipe = pipes[i]
    pipe[1]:close()
    pipe[2]:close()
  end

  return res
end

local function main(npipes)
  num_pipes = tonumber(npipes) or 100

  assert(sys.limit_nfiles(num_pipes * 2 + 50))

  local evq = assert(sys.event_queue())

  for i = 1, 25 do
    print(run_once(evq))
  end

  sys.exit(0)
end

main(...)
