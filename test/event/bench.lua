#!/usr/bin/env lua

-- libevent/test/bench.c


local sys = require"sys"
local sock = require"sys.sock"


local USE_PIPES = false

local pipes = {}
local events, events_idx = {}, {}
local count, writes, fired
local num_pipes, num_active, num_writes

local period = sys.period()


local function read_cb(evq, evid, fd)
    count = count + (fd:read(1) and 1 or 0)

    if writes ~= 0 then
	local widx = events_idx[evid] + 1
	if widx > num_pipes then
	    widx = widx - num_pipes
	end

	local fdo = pipes[widx][2]
	fdo:write("e")
	writes = writes - 1
	fired = fired + 1
    end
end

local function run_once(evq)
    for i = 1, num_pipes do
	local fdi = pipes[i][1]
	local evid = events[i]
	if evid then
	    evq:del(evid, true)
	end
	evid = evq:add(fdi, "r", read_cb)
	if not evid then
	    error(errorMessage)
	end
	events[i], events_idx[evid] = evid, i
    end

    evq:loop(0, true)

    fired = 0
    local space = math.floor(num_pipes / num_active)
    for i = 0, num_active - 1 do
	local fdo = pipes[i * space + 1][2]
	fdo:write("e")
	fired = fired + 1
    end

    count = 0
    writes = num_writes

    local xcount = 0
    period:start()
    repeat
	evq:loop(0, true)
	xcount = xcount + 1
    until (count == fired)
    local res = period:get()

    if xcount ~= count then
	sys.stderr:write("Xcount: ", xcount, ", Rcount: ", count, "\n")
    end
    return res
end

local function main(npipes, nactives, nwrites)
    num_pipes = tonumber(npipes) or 100
    num_active = tonumber(nactives) or 1
    num_writes = tonumber(nwrites) or 100

    assert(sys.limit_nfiles(num_pipes * 2 + 50))

    for i = 1, num_pipes do
	local fdi, fdo, res
	if USE_PIPES then
	    fdi, fdo = sys.handle(), sys.handle()
	    res = fdi:pipe(fdo)
	else
	    fdi, fdo = sock.handle(), sock.handle()
	    res = fdi:socket(fdo)
	end
	if not res then
	    error(errorMessage)
	end
	pipes[i] = {fdi, fdo}
    end

    local evq = assert(sys.event_queue())

    for i = 1, 25 do
	print(run_once(evq))
    end
end

main(...)
