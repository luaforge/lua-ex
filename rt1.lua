#!/usr/bin/env lua
require "ex"

--print"os.sleep"
--os.sleep(2);

print"os.setenv"
assert(os.setenv("foo", "42"))
print("foo=", os.getenv("foo"))
assert(os.unsetenv("foo"))
print("foo=", os.getenv("foo"))


