#!/usr/bin/env lua

local string = require"string"
local sys = require"sys"

local thread = sys.thread

thread.init()


-- Pipes
local work_pipe, main_pipe = thread.pipe(), thread.pipe()

-- Consumer VM-Thread
do
    local function consume(work_pipe, main_pipe)
	local sys = require"sys"
	local thread = sys.thread

	while true do
	    local i, s = work_pipe:get(200)
	    if not i then break end
	    print(i, s)
	    thread.sleep(200)
	end
	main_pipe:put("The end.")
    end

    assert(thread.runvm(string.dump(consume), work_pipe, main_pipe))
end

-- Producer VM-Thread
do
    local function produce(work_pipe)
	local sys = require"sys"
	local thread = sys.thread

	for i = 1, 10 do
	    work_pipe:put(i, (i % 2 == 0) and "even" or "odd")
	    thread.sleep(100)
	end
    end

    assert(thread.runvm(string.dump(produce), work_pipe))
end

main_pipe:wait()  -- Wait VM-Threads termination
