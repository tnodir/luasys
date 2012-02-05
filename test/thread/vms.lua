#!/usr/bin/env lua

local string = require"string"
local sys = require"sys"

local thread = sys.thread

thread.init()


local COUNT = 1000000

-- Pipe
local work_pipe = thread.pipe()

-- Producer VM-Thread
do
    local function produce(work_pipe, COUNT)
	local string = require"string"
	local sys = require"sys"

	local rep = string.rep
	local thread = sys.thread
	local rand = assert(sys.random())

	for i = 1, COUNT do
	    local d = i % 10
	    local r = rand(460)
	    local data = rep(d, r + 1)
	    work_pipe:put(i, r, data)
	end
    end

    assert(thread.runvm(nil, string.dump(produce), work_pipe, COUNT))
end

-- Consumer VM-Thread
do
    local function consume(work_pipe, COUNT)
	local string = require"string"
	local sys = require"sys"

	local rep = string.rep
	local thread = sys.thread

	local count = 0
	for i = 1, COUNT do
	    local d = i % 10
	    local num, r, s = work_pipe:get()
	    if num == false then break end
	    if num ~= i then
		error(i .. " number expected, got " .. num)
	    end
	    local data = rep(d, r + 1)
	    if s ~= data then
		error(data .. " string expected, got " .. s)
	    end
	    count = count + 1
	end
	assert(count == COUNT, COUNT .. " messages expected, got " .. count)
    end

    assert(thread.runvm(nil, string.dump(consume), work_pipe, COUNT))
end

-- Wait VM-Threads termination
assert(thread.self():wait())
