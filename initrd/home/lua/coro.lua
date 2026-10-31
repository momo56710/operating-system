--@ title: Coroutines
--@ about: generators and pipelines
local function range(n) return coroutine.wrap(function() for i = 1, n do coroutine.yield(i) end end) end
local function filter(pred, gen) return coroutine.wrap(function() for v in gen do if pred(v) then coroutine.yield(v) end end end) end
local function map(f, gen) return coroutine.wrap(function() for v in gen do coroutine.yield(f(v)) end end) end
local out = {}
for v in map(function(x) return x * x end, filter(function(x) return x % 2 == 1 end, range(15))) do out[#out + 1] = v end
print("odd squares:", table.concat(out, " "))
local producer = coroutine.create(function() for _, w in ipairs { "alpha", "beta", "gamma" } do coroutine.yield(w) end return "done" end)
while true do
  local ok, v = coroutine.resume(producer)
  print(coroutine.status(producer), v)
  if coroutine.status(producer) == "dead" then break end
end
