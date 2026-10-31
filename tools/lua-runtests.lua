-- Run Lua's official test files one by one, each in a protected call, and report PASS/FAIL per file.
_soft = true
_port = true
_nomsg = true
local names = { "strings", "literals", "tpack", "attrib", "locals", "constructs", "code", "cstack", "nextvar", "pm", "utf8",
                "events", "vararg", "closure", "coroutine", "goto", "errors", "math", "sort", "bitwise", "calls", "db", "files", "gc", "api" }
if arg[1] then names = { table.unpack(arg) } end
local pass, fail = 0, 0
for _, name in ipairs(names) do
  io.write("\n##### ", name, "\n"); io.stdout:flush()
  local t0 = os.clock()
  local ok, err = pcall(dofile, name .. ".lua")
  local dt = os.clock() - t0
  if ok then
    pass = pass + 1
    print(string.format("RESULT %-12s PASS  (%.1fs)", name, dt))
  else
    fail = fail + 1
    print(string.format("RESULT %-12s FAIL  %s", name, tostring(err):sub(1, 400)))
  end
  collectgarbage()
end
print(string.format("ALL TESTS FINISHED: %d passed, %d failed", pass, fail))
