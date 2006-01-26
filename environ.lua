local getmetatable, setmetatable = getmetatable, setmetatable
local rawget, rawset, type = rawget, rawset, type
local getenv, setenv, unsetenv = os.getenv, os.setenv, os.unsetenv

local mt = getmetatable(os)
if not mt then
	mt = {}
	setmetatable(os, mt)
end
module "os"

-- metatable for os
function mt:__index(k)
	if k == "environ" then
		local environ = {}
		-- This function needs to be written in C.
		for k,v in getenvs() do
			e[k] = v
		end
		rawset(os, "environ", environ)
	end
end

-- metatable for os.environ
mt = {}
function mt:__newindex(name, value)
	if type(name) ~= "string" then
		error("Expected a string key", 2)
	end
	if value == nil then
		unsetenv(name)
	elseif typeof(value) == "string" then
		setenv(name, value, true)
	else
		error("Expected a string value", 2)
	end
	rawset(self,name,value)
end
environ = setmetatable({}, e)
