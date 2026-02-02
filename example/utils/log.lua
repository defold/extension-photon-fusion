local M = {}


local buffer = {}

local _print = _G.print

local function logstring(s)
	table.insert(buffer, 1, s)
	table.remove(buffer, 100)
	_print(s)
end

function M.log(fmt, ...)
	local s = string.format(tostring(fmt), ...)
	table.insert(buffer, 1, s)
	table.remove(buffer, 100)
	_print(s)
end

_G.print = function(...)
	local s = ""
	local t = { ... }
	for i=1,#t do
		s = s .. tostring(t[i]) .. "    "
	end
	logstring(s)
end



function M.latest(n)
	local i = 1
	n = n or 20
	return function()
		local ret = nil
		if i <= n then
			ret = buffer[i]
			i = i + 1
		end
		return ret
	end
end

return setmetatable(M, {
	__call = function(t, ...)
		return M.log(...)
	end
})