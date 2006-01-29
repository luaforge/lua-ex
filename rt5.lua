#!/usr/bin/env lua
require "ex"

local proc
proc = assert(os.spawn(arg[1]))
print(proc)
print(assert(proc:wait()))
