#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


-- Consumer VM-Thread
local consumer
do
    local function consume(master)
	local sys = require"sys"
	local thread = sys.thread

	while true do
	    local producer, i, s = thread.msg_recv(200)
	    if not producer then break end
	    print(i, s)
	    thread.sleep(200)
	end
	thread.msg_send(master, "end")
    end

    consumer = assert(thread.runvm(string.dump(consume)))
end

-- Producer VM-Thread
do
    local function produce(master, consumer)
	local sys = require"sys"
	local thread = sys.thread

	for i = 1, 10 do
	    thread.msg_send(consumer, i, (i % 2 == 0) and "even" or "odd")
	    thread.sleep(100)
	end
    end

    assert(thread.runvm(string.dump(produce), consumer))
end

thread.msg_recv()  -- Wait vm-threads termination
