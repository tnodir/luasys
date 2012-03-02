#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"

local thread = sys.thread
assert(thread.init())


local NWORKERS = 3
local NTASKS = 3

local evq = assert(sys.event_queue())
local sched = assert(thread.scheduler(evq))

-- Scheduler workers
do
	local function process(id)
		print("worker", id)
		local _, err = sched:loop(100)
		if err then error(err) end
		print("worker end", id)
	end

	for i = 1, NWORKERS do
		assert(thread.run(process, i))
	end
end

-- Put tasks to scheduler
do
	local function task(id)
		print("task", id)
		coroutine.yield()
		print("task end", id)
	end

	for i = 1, NTASKS do
		local co = assert(thread.coroutine())
		assert(sched:put(co, task, i))
	end
end

-- Wait Threads termination
assert(thread.self():wait())

