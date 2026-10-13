	TOTAL_NUMBERS = 10000
	TOTAL_TIMES = 4

    function mk_float(i)
        return i + (i/TOTAL_NUMBERS)
    end

    function update(nums)
        local summ = 0
        for i = 1, TOTAL_TIMES do
            for i,num in ipairs(nums) do
                summ = summ + tonumber(num)
            end
        end
        return summ
    end

  nums = {}
  for i = 1, TOTAL_NUMBERS do
    table.insert(nums, tostring(mk_float(i)))
  end

  loadfile("profile.lua")()
  ---
    local result
    io.write(string.format("\"string2float\", %.8f, %d\n", profile_it(PROFILE_RUNS, function () result = update(nums) end), PROFILE_N))
    print("checksum " .. string.format("%.6f", result))

