#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"


local evq = assert(sys.event_queue())

local pipe1, pipe2, pipe3


local function read_cb(evq, evid, fd)
  fd:read(1)
  evq:del(evid)
  if pipe3 then
    pipe2[2]:write("e")
    evq:del(pipe3[3])
    pipe3[1]:close()
    pipe3[2]:close()
    pipe3 = nil
  end
end

local function create_socketpair()
  local pipe = {sock.handle(), sock.handle()}
  assert(pipe[1]:socket(pipe[2]))
  pipe[3] = assert(evq:add_socket(pipe[1], "r", read_cb))
  return pipe
end

pipe1 = create_socketpair()
pipe2 = create_socketpair()
pipe3 = create_socketpair()

pipe1[2]:write("e")

evq:loop()

print("OK")
