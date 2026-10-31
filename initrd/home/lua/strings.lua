--@ title: Strings
--@ about: patterns, gsub, format
local s = "The quick brown fox jumps over the lazy dog"
print(#s, s:upper(), s:reverse())
print(s:find("brown"), s:match("(%a+) dog"))
print((s:gsub("%a+", function(w) return w:sub(1, 1):upper() .. w:sub(2) end)))
for k, v in ("a=1, b=22, c=333"):gmatch("(%w+)=(%w+)") do io.write(k, "->", v, " ") end print()
print(("%5.2f|%-8s|%08.3f|%x|%q"):format(3.14159, "left", 2.5, 255, 'a "quoted" line'))
print(("x"):rep(5, "-"), ("abc"):byte(1, -1))
print(utf8.char(72, 228, 8364), utf8.len("häl"))
print(os.date("!%Y-%m-%d %H:%M:%S", 0), tostring(1e15), 7 // 2, 7 / 2, 2^0.5, 10 % 3, -7 // 2)
