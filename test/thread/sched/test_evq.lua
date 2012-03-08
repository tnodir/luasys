#!/usr/bin/env lua

local sys = require("sys")
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


local evq = assert(sys.event_queue())

-- Scheduler
local sched, sched_stop
do
	local function controller()
		if sched_stop and sched:empty() then
			sched:stop()
			evq:stop()
		end
	end

	sched = assert(thread.scheduler(controller))

	-- Workers
	assert(thread.run(sched.loop, sched))
end


print("-- Timer event")
do
	local timeout = 100

	local function yield_timeout()
		local _, _, T = sched:wait_timer(evq, timeout)
		assert(T == timeout,
			timeout .. " msec expected, got " .. tostring(T))
	end

	assert(sched:put(yield_timeout))
end


print("-- Socket event")
do
	local msg = "test"

	local function on_read(fd)
		local R, W, T, EOF = sched:wait_socket(evq, fd, "r")
		assert(R)
		local s = assert(fd:read())
		assert(s == msg, msg .. " expected, got " .. s)
	end

	local fdi, fdo = sock.handle(), sock.handle()
	assert(fdi:socket(fdo))

	assert(sched:put(on_read, fdi))

	thread.sleep(100)
	assert(fdo:write(msg))
end


sched_stop = true

assert(evq:add_signal("INT", evq.stop))
assert(evq:loop())
print("OK")

