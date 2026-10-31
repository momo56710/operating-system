--@ title: Hanoi
--@ about: Tower of Hanoi moves
--@ args: Disks (blank = 4)
local moves = 0
local function go(n, a, b, c)
  if n == 0 then return end
  go(n - 1, a, c, b)
  moves = moves + 1
  if moves <= 40 then print(("move disk %d from %s to %s"):format(n, a, c)) end
  go(n - 1, b, a, c)
end
local n = tonumber(arg[1]) or 4
go(n, "A", "B", "C")
print(moves .. " moves for " .. n .. " disks (2^" .. n .. " - 1)")
