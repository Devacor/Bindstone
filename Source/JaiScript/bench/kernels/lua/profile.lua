PROFILE_RUNS = 10

local clock = os.clock
if profile_native and profile_native.clock then
  clock = profile_native.clock
elseif jit and jit.os == "Windows" then
  local ffi = require("ffi")
  ffi.cdef[[
    int QueryPerformanceCounter(int64_t * count);
    int QueryPerformanceFrequency(int64_t * freq);
  ]]
  local freq = ffi.new("int64_t[1]")
  ffi.C.QueryPerformanceFrequency(freq)
  local scale = 1.0 / tonumber(freq[0])
  local count = ffi.new("int64_t[1]")
  clock = function()
    ffi.C.QueryPerformanceCounter(count)
    return tonumber(count[0]) * scale
  end
end

PROFILE_BUDGET = 0.5
PROFILE_N = 1

function profile_it(profiles, fn)
  local n = 1
  local total
  while true do
    local start = clock()
    for i = 1, n do fn() end
    total = clock() - start
    if total >= PROFILE_BUDGET or n >= 1000000000 then break end
    local per = math.max(total / n, 1e-9)
    local next = math.floor(PROFILE_BUDGET / per * 1.2)
    next = math.min(next, 100 * n)
    next = math.max(next, n + 1)
    n = math.min(next, 1000000000)
  end
  PROFILE_N = n
  return total / n
end
