#!/usr/bin/env lua

local sys = require("sys")


print"-- Coroutines"
do
    local evq = assert(sys.event_queue())

    local function sleep(msg)
	print(msg, coroutine.yield())
    end

    local function start(co)
	local evid = assert(evq:add_timer(co, 10))
	sleep"init"
	assert(evq:timeout(evid, 20))
	sleep"work"
	assert(evq:timeout(evid, 30))
	sleep"done"
	assert(evq:del(evid))
    end

    for i = 1, 3 do
	local co = assert(coroutine.create(start))
	assert(coroutine.resume(co, co))
    end

    evq:loop()
    print"OK"
end


print"-- Signal: wait SIGINT"
do
    local function on_signal(evq, evid, _, _, _, timeout)
	if timeout then
	    assert(evq:timeout(evid))
	    assert(evq:ignore_signal("INT", false))
	    print"SIGINT enabled. Please, press Ctrl-C..."
	else
	    print"Thanks!"
	end
    end

    local evq = assert(sys.event_queue())

    assert(evq:add_signal("INT", on_signal, 3000, true))
    assert(evq:ignore_signal("INT", true))

    evq:loop(10000)
    print"OK"
end


