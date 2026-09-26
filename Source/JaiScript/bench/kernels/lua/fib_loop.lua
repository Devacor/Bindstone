local function fibI(n)
    local last = 0
    local cur = 1
    n = n - 1
    while (n > 0)
    do
        n = n - 1
        local tmp = cur
        cur = last + cur
        last = tmp
    end
    return cur
end
loadfile("profile.lua")()

local result
io.write(string.format("\"fibonacci loop\", %.8f, %d\n", profile_it(PROFILE_RUNS, function () result = fibI(6511134) end), PROFILE_N))
print("checksum " .. tostring(result % 4294967296))

