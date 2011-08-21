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

-- Worker Thread
local worker
do
    local function read_stdin()
	while true do
	    local line = stdin:read()
	    if #line <= 2 then
		print("Worker:", "Interrupt event queue")
		evq:interrupt()
	    else
		sys.stdout:write("Worker:\tInput: ", line)
	    end
	end
    end

    local function start()
	local ok, err = pcall(read_stdin)
	assert(not ok and thread.self():interrupted(err))
	print("Worker:", "Terminated")
	return -1
    end

    worker = assert(thread.run(start))
end

-- Timer
local timer
do
    local function on_event(evq, evid)
	if worker then
	    print("Main:", "Timer")
	else
	    print("Main:", "Worker closed -> Delete timer")
	    evq:del(evid)
	end
    end

    timer = evq:add_timer(on_event, 3000)
end

-- Interrupt handler
do
	local function on_intr()
	    print("Main:", "Event queue interrupted -> Close stdin")
	    stdin:close(true)
	    worker:interrupt()
	    assert(worker:wait() == -1)
	    worker = nil
	    evq:notify(timer)
	end

	evq:on_interrupt(on_intr)
end

evq:loop()
