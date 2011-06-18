#!/usr/bin/env lua

-- Lua Internet Server Application: FastCGI Launcher

local sys = require("sys")
local sock = require("sys.sock")

local fcgi_decode, fcgi_encode = sys.fcgi_decode, sys.fcgi_encode

sys.thread.init()


local bind = {
    [8080] = "127.0.0.1",
}

local BUFER_OUT_SIZE = 32768
local BUFER_IN_SIZE = 8192


-- Event Queue
local evq = assert(sys.event_queue())


-- Log message
local log
do
    local stderr = sys.stderr

    log = function(title, msg)
	stderr:write(title, ": ", msg or errorMessage, "\n")
    end
end


-- Request's metatable
local request_meta = {}
do
    -- Request keys: CGI variables (in upper case),
    --   "keep_conn", "stdin" (data),
    --   "id" (request_id), "channel" (reference),
    --   "error" (message), "next_ready" (request_id),
    --   "state" (state of request processing: "headers", "out"),
    --   "prev" (last written record)

    request_meta.__index = request_meta

    -- Get header variable
    request_meta.getvar = function(req, name)
	return req[name]
    end

    -- Data from stdin
    request_meta.data = function(req)
	return req.stdin
    end

    -- Read rest data
    request_meta.read = function(req)
    end

    -- Output
    local function out(req, s)
	local chan = req.channel
	local buffer_out = chan.buffer_out

	chan.prev = fcgi_encode(buffer_out, req.id, chan.prev, s)

	if buffer_out:seek() > BUFER_OUT_SIZE
	    and chan.fd:write(buffer_out) ~= nil
	then
		chan.prev = nil
	end
    end

    -- Write
    request_meta.write = function(req, s, ...)
	if req.state ~= "out" then
	    out(req, req.state == "headers" and "\r\n"
		or "Content-type: text/html\r\n\r\n")
	    req.state = "out"
	end

	if s and s ~= "" then out(req, s) end

	if select('#', ...) ~= 0 then
	    return req:write(...)
	end
    end

    -- Set header variable
    request_meta.header = function(req, name, value)
	if req.state == "out" then
	    error"Headers already sent"
	end

	out(req, name .. ": " .. value .. "\r\n")
    end

    -- Close the request
    request_meta.close = function(req)
	local chan = req.channel
	local fd, buffer_out = chan.fd, chan.buffer_out

	chan.prev = nil
	fcgi_encode(buffer_out, req.id)

	if not fd:write(buffer_out) then
	    log("socket.write")
	    buffer_out:seek(0)
	end
    end
end


-- Channels (Connections with front-end)
local channels = {}
do
    -- Pool of free channels
    local pool = setmetatable({n = 0}, {__mode = "v"})

    local function pool_get()
	local n = pool.n
	local chan = pool[n]
	if not chan then
	    n = 1
	    chan = {
		request_meta = request_meta,
		fd = sock.handle()
	    }

	    local buf = sys.mem.pointer():alloc(BUFER_OUT_SIZE)
	    if not buf then return end
	    chan.buffer_out = buf

	    buf = sys.mem.pointer():alloc(BUFER_IN_SIZE)
	    if not buf then return end
	    chan.buffer_in = buf
	end
	pool.n = n - 1
	return chan
    end

    local function pool_put(chan)
	local n = pool.n + 1
	pool.n, pool[n] = n, chan
    end


    -- Get the free channel
    channels.get = pool_get

    -- Put the free channel
    channels.put = pool_put

    -- Close the channel
    channels.close = function(evq, evid, fd)
	local chan = channels[fd]
	channels[fd] = nil
	pool_put(chan)

	evq:del(evid)
	fd:close()
    end
end


-- Directory monitoring and files caching
local dir_loadfile
do
    local Match = string.match
    local dirs = {}

    -- Directory changed
    local function on_change(evq, evid, dir)
	evq:del(evid)
	dirs[dir] = nil
    end

    -- Get file from directory or from cache
    dir_loadfile = function(path)
	local dir, filename = Match(path, [[(.-)([^/\]+)$]])
	local dir_files = dirs[dir]
	local is_watch = true
	local chunk, err

	if dir_files then
	    chunk = dir_files[filename]
	elseif evq:add_dirwatch(dir, on_change, true) then
	    dir_files = {}
	    dirs[dir] = dir_files
	else
	    is_watch = false
	end
	if not chunk then
	    chunk, err = loadfile(path)
	    if chunk and is_watch then
		dir_files[filename] = chunk
	    end
	end
	return chunk, err
    end
end


-- Process requests
local process
do
    local xpcall, traceback = sys.xpcall, debug.traceback

    -- Process request
    local function process_request(req)
	local path = req:getvar"PATH_TRANSLATED"
	local chunk, err = dir_loadfile(path)
	local status

	req.state = "headers"
	if not err then
	    status, err = xpcall(traceback, chunk, req)
	else
	    req:header("Status", 500)
	end
	if not status then
	    req:write("\n\n<pre>", err)
	end

	req:close()
	return status
    end

    -- Process requests
    process = function(evq, evid, fd, R, W, T, EOF)
	local chan = channels[fd]
	local buffer_in = chan.buffer_in
	local status

	if not EOF and fd:read(buffer_in) then
	    local request_id = fcgi_decode(buffer_in, chan)

	    while request_id do
		local req = chan[request_id]

		if req.error then
		    chan[request_id] = nil
		    log("req.error", req.error)
		else
		    status = process_request(req)
		end

		status = req.keep_conn
		request_id = req.next_ready
	    end
	end
	if not status then
	    channels.close(evq, evid, fd)
	end
    end
end


-- Accept new client
local function accept(evq, evid, fd)
    local channel = channels.get()
    if not channel then
	log("channels.get")
	return
    end

    fd = fd:accept(channel.fd)
    if not fd then
	log("accept")
	return
    end

    --fd:nonblocking(true)
    --fd:sockopt("tcp_nodelay", 1)

    evid = evq:add_socket(fd, 'r', process)
    if not evid then
	fd:close()
	channels.put(channel)
	log("evq.add")
	return
    end

    channels[fd] = channel
end


local saddr = sock.addr()
for port, host in pairs(bind) do
    local fd = sock.handle()
    assert(fd:socket())
    assert(fd:sockopt("reuseaddr", 1))
    assert(saddr:inet(port, sock.inet_pton(host)))
    assert(fd:bind(saddr))
    assert(fd:listen())
    assert(evq:add_socket(fd, 'r', accept))
end

-- Quit by Ctrl-C
assert(evq:add_signal("INT", evq.stop))

evq:loop()
