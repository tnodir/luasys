#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


-- Event Queue
local evq = assert(sys.event_queue())

-- Data Pool
local dpool
do
    local function on_full(dpool, ...)
	print("put on full>", ...)
	return ...
    end

    local function on_empty()
	print("get on empty>")
    end

    dpool = assert(thread.data_pool())
    dpool:callbacks(on_full, on_empty)
    dpool:max(2)  -- set maximum watermark
end

-- Consumer Thread
do
    local function consumer()
	while true do
	    local i, s = dpool:get(200)
	    if not i then break end
	    print(i, s)
	    thread.sleep(200)
	end
    end

    local tid = assert(thread.run(consumer))
    assert(evq:add_trigger(tid, thread))
end

-- Producer Thread
do
    local function producer()
	for i = 1, 10 do
	    dpool:put(i, (i % 2 == 0) and "even" or "odd")
	    thread.sleep(100)
	end
    end

    local tid = assert(thread.run(producer))
    assert(evq:add_trigger(tid, thread))
end

evq:loop()  -- Wait threads termination
