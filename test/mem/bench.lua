#!/usr/bin/env lua

local sys = require("sys")

local mem = sys.mem


-- Copy file
local infile = ...
local outfile = "bench.out"

if not infile then
	print[[Usage: bench.lua <FILENAME>]]
	return
end


local BUFSIZ = 65536

local period = sys.period()

period:start()
do
	local fi = assert(io.open(infile, "rb"))
	local fo = assert(io.open(outfile, "wb"))
	while true do
		local data = fi:read(BUFSIZ)
		if not (data and fo:write(data)) or #data < BUFSIZ then
			break 
		end
	end
	fi:close()
	fo:close()
	os.remove(outfile)
end
print("os.io=", period:get())

period:start()
do
	local fi = assert(sys.handle():open(infile))
	local fo = assert(sys.handle():create(outfile))
	while true do
		local data = fi:read(BUFSIZ)
		if not (data and fo:write(data)) or #data < BUFSIZ then
			break 
		end
	end
	fi:close()
	fo:close()
	sys.remove(outfile)
end
print("sys.io=", period:get())

period:start()
do
	local fi = assert(sys.handle():open(infile))
	local fo = assert(sys.handle():create(outfile))
	local buf = assert(mem.pointer(BUFSIZ))
	while true do
		local len = fi:read(buf, BUFSIZ)
		if not (len and fo:write(buf)) or len < BUFSIZ then
			break
		end
	end
	fi:close()
	fo:close()
	sys.remove(outfile)
end
print("sys.io.buf=", period:get())

