#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


-- Usage notes
print[[
Press <Enter> to quit or any chars for feedback...
]]


local stdin = sys.stdin

-- Event Queue
local evq = assert(sys.event_queue())

local controller

-- Worker Thread
local worker
do
    local function read_stdin()
	while true do
	    local line = stdin:read()
	    if #line <= 2 then
		print("Worker:", "Notify controller")
		evq:notify(controller)
	    else
		sys.stdout:write("Worker:\tInput: ", line)
	    end
	end
    end

    local function start()
	local _, err = pcall(read_stdin)
	if not thread.self():interrupted(err) then
	    print(err)
	    error("Thread Interrupt Error expected")
	end
	print("Worker:", "Terminated")
	return -1
    end

    worker = assert(thread.run(start))
end

-- Controller
do
    local function on_event(evq, evid)
	print("Controller:", "Close stdin")
	stdin:close(true)
	worker:interrupt()
	assert(worker:wait() == -1)
	worker = nil
	evq:del(evid)
    end

    controller = assert(evq:add_timer(on_event, 30000))
end

evq:loop()
