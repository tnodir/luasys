#!/usr/bin/env lua

-- libevent/test/test-time.c


local sys = require"sys"


local NEVENT = 20000

local called = 0

local timers = {}

local rand_int = math.random
math.randomseed(os.time())

local function time_cb(evq, evid, idx)
    evq:del(evid)
    timers[idx] = nil

    called = called + 1
    if called < 10 * NEVENT then
	for i = 1, 10 do
	    local j = rand_int(NEVENT)
	    local msec = rand_int(50) - 1

	    evid = timers[j]
	    if msec % 2 == 0 then
		if evid then
		    evq:del(evid)
		    timers[j] = nil
		end
	    elseif evid then
		evq:timeout(evid, msec)
	    else
		timers[j] = evq:add_timer(time_cb, msec, j)
	    end
	end
    end
end

do
    local evq = assert(sys.event_queue())

    for i = 1, NEVENT do
	timers[i] = evq:add_timer(time_cb, rand_int(50) - 1, i)
    end

    evq:loop()

    print("called", called)
    return (called < NEVENT)
end

