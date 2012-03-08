#!./lua
--#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread
assert(thread.init())


local DEBUG = false
local NWORKERS = 3
local NTASKS = 100000

-- Scheduler
local sched
do
	local function controller(co, err)
		if not co then return end
		print("task end", co, err or "")
	end

	sched = assert(thread.scheduler(
		DEBUG and controller or nil))
end

-- Put tasks to scheduler
do
	local function process()
		if DEBUG then
			print("task", coroutine.running())
		end
		coroutine.yield()
	end

	for i = 1, NTASKS do
		assert(sched:put(process))
	end
end

print("<Enter> to start...")
sys.stdin:read()

-- Scheduler workers
do
	local function loop()
		print("worker", (thread.self()))
		local _, err = sched:loop(100)
		if err then error(err) end
		print("worker end", (thread.self()))
	end

	for i = 1, NWORKERS do
		assert(thread.run(loop))
	end
end

-- Wait Threads termination
assert(thread.self():wait())

