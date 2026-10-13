local function fibR(n)

    if (n < 2) then return n end
    return (fibR(n-2) + fibR(n-1))
end

loadfile("profile.lua")()

local result
io.write(string.format("\"fibonacci recursive\", %.8f, %d\n", profile_it(PROFILE_RUNS, function () result = fibR(31) end), PROFILE_N))
print("checksum " .. string.format("%.0f", result))
