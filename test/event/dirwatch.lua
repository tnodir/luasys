#!/usr/bin/env lua

local sys = require"sys"


local filename = "test"

local fd = sys.handle()

local function on_change(evq, evid, path)
    fd:close()
    sys.remove(filename)
    evq:del(evid)
    print"OK"
end

local evq = assert(sys.event_queue())

assert(evq:add_dirwatch(".", on_change))

assert(fd:create(filename))

evq:loop()
