local addOne
if jit then
    local ffi = require("ffi")
    ffi.cdef[[
    int addOne(int);
    ]]
    -- addOne is staged beside the interpreter; resolve it from the interpreter's own path, never from cwd
    local exeIndex = -1
    while arg and arg[exeIndex - 1] do exeIndex = exeIndex - 1 end
    local exe = (arg and arg[exeIndex]) or ""
    local exeDir = exe:match("^(.*)[/\\][^/\\]*$") or "."
    local libName = jit.os == "Windows" and "addOne.dll" or (jit.os == "OSX" and "libaddOne.dylib" or "libaddOne.so")
    addOne = ffi.load(exeDir .. "/" .. libName).addOne
else
    addOne = profile_native.addOne
end

local function testAdds()
    local count = 0
    for i = 1, 1000000 do
        count = addOne(count)
    end
    return count
end

local result
function test()
    local count = testAdds()
    if count ~= 1000000 then print("failed\n", count) end
    result = count
end

loadfile("profile.lua")()

io.write(string.format("\"interop host calls\", %.8f, %d\n", profile_it(PROFILE_RUNS, test), PROFILE_N))
print("checksum " .. string.format("%.0f", result))
