local function expLoop(n)

    local sum = 0
    for i = 0, n do
      sum = sum + math.exp(1./(1.+i))
    end
    return sum
end
loadfile("profile.lua")()

local result
io.write(string.format("\"exp loop\", %.8f, %d\n", profile_it(PROFILE_RUNS, function () result = expLoop(1000000) end), PROFILE_N))
print("checksum " .. string.format("%.6f", result))
