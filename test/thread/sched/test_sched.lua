#!/usr/bin/env lua

local sys = require("sys")
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


-- Scheduler
local sched, sched_stop
do
	local function controller(co)
		if co and sched_stop and sched:empty() then
			sched:stop()
		end
	end

	sched = assert(thread.scheduler(controller))

	-- Workers
	assert(thread.run(sched.loop, sched))
end


print("-- Suspend/Resume")
do
	local msg = "test"

	local function test()
		local s = sched:suspend()
		assert(s == msg, msg .. " expected, got " .. tostring(s))
	end

	local co = assert(sched:put(test))

	thread.sleep(100)
	sched:resume(co, msg)
end


sched_stop = true

assert(sched:loop())
print("OK")

