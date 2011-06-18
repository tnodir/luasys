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

-- Thread
local thread_id
do
    local function read_stdin()
	while true do
	    local line = stdin:read()
	    if #line <= 2 then
		print("> Worker thread:", "Interrupt event queue")
		evq:interrupt()
	    else
		sys.stdout:write(">Worker thread input:\t", line)
	    end
	end
    end

    thread_id = assert(thread.run(read_stdin))
end

-- Timer
do
    local function on_event(evq, evid)
	if thread_id then
	    print("Main thread:", "Timer")
	else
	    print("Main thread:", "Worker thread closed", "-> Delete timer")
	    evq:del(evid)
	end
    end

    evq:add_timer(on_event, 2000)
end

-- Interrupt handler
do
	local function on_intr()
	    print("Main thread:", "Event queue interrupted", "-> Close stdin")
	    stdin:close()
	    thread.interrupt(thread_id)
	    thread_id = nil
	end

	evq:on_interrupt(on_intr)
end

evq:loop()
