-- Win32: Test Beeper Service

local sys = require("sys")

local win32 = sys.win32


local TIMEOUT = 3000  -- milliseconds

local FILENAME = [[C:\tmp\lua_msvc.51\bin\beep_svc.lua]]  -- EDIT PATH !!!

local USE_FOREVER_LOOP = true


local svc_name = "Beeper.Lua"

while true do
	local action = ...
	if action == "service" then
		break
	elseif action == "install" then
		if not win32.service.install(svc_name, FILENAME .. " service", true) then
			error(errorMessage)
		end
		-- Set Description
		local r = win32.registry()
		if r:open("HKEY_LOCAL_MACHINE",
				[[System\CurrentControlSet\Services\]] .. svc_name, "w") then
			r:set("Description", "Beeper - Lua Test Service")
			r:close()
		end
	elseif action == "uninstall" then
		local res, working = win32.service.uninstall(svc_name)
		if not res then
			error(working and "Stop the service first." or errorMessage)
		end
	else
		return print("Usage: lua.exe beep_svc.lua (un)install")
	end
	return print("Service '" .. svc_name .. "' " .. action .. "ed.")
end


local svc = assert(win32.service.handle(svc_name, true))

if USE_FOREVER_LOOP then
	-- Simple Loop
	local timeout = TIMEOUT
	while true do
		local res = svc:wait(timeout)
		if not res then
			if res == nil then
				break  -- Fail
			end
			win32.beep()  -- Timeout
		else
			res = svc:status()
			if res == "stop" then
				break
			elseif res == "pause" then
				timeout = nil
				svc:status"paused"
			elseif res == "continue" then
				timeout = TIMEOUT
				svc:status"running"
			end
		end
	end
else
	-- Event Queue
	local function on_event(evq, evid, svc, _, _, timeout)
		if timeout then
			win32.beep()
		else
			local res = svc:status()
			if res == "stop" then
				evq:del(evid)
			elseif res == "pause" then
				evq:timeout(evid)
				svc:status"paused"
			elseif res == "continue" then
				evq:timeout(evid, TIMEOUT)
				svc:status"running"
			end
		end
	end

	local evq = assert(sys.event_queue())
	evq:add(svc, 'r', on_event, TIMEOUT)
	evq:loop()
end

svc:status"stopped"

