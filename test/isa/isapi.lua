-- Lua Internet Server Application: ISAPI Extension Launcher

local root = ...

-- Set C-modules placement
do
    local cpath = root:gsub([[^\\%?\]], "")

    package.cpath = package.cpath
	.. cpath .. "\\?.dll;"
	.. cpath .. "\\?51.dll;"
	.. cpath .. "\\lua5.1.dll;"
end


local sys = require("sys")

sys.thread.init()


local function process(ecb)
    local path = ecb:getvar"PATH_TRANSLATED"
    local chunk, err = loadfile(path)
    if err then error(err) end
    chunk(ecb)
end

return process
