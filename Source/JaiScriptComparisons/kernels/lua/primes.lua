function isprime(n)
    for i = 2, (n - 1) do
        if (n % i == 0) then
            return false
        end
    end
    return true
end


local function primes(n)
    local count = 0

    for i = 2, n do
        if (isprime(i)) then
            count = count + 1
        end
    end
    return count
end

loadfile("profile.lua")()

local result
io.write(string.format("\"primes loop\", %.8f, %d\n", profile_it(PROFILE_RUNS, function () result = primes(14000) end), PROFILE_N))
print("checksum " .. string.format("%.0f", result))

