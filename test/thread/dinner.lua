#!/usr/bin/env lua

local sys = require"sys"

local thread = sys.thread

thread.init()


local NFORKS = 5
local NPHILOSOPHERS = NFORKS
local NSPAGHETTI = 3

-- Forks
local forks
do
    forks = assert(thread.data_pool())
    for i = 1, NFORKS do
	forks:put(1)
    end
end

-- Philosophers
do
    local n = NPHILOSOPHERS
    local rand = assert(sys.random())

    local function philosopher(num)
	print(num, "begin")
	for i = 1, NSPAGHETTI do
	    forks:get()
	    if forks:get(0) then
		print(num, "eat")
		forks:put(1)
	    else
		print(num, "skip")
	    end
	    forks:put(1)
	    thread.sleep(rand(10))
	end
	print(num, "end.")

	n = n - 1
	if n == 0 then
	    print"The End."
	end
    end

    for i = 1, NPHILOSOPHERS do
	assert(thread.run(philosopher, i))
    end
end

sys.stdin:read()
