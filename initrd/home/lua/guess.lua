--@ title: Guess
--@ about: guess my number
math.randomseed(os.time())
local secret, tries = math.random(1, 100), 0
print("I'm thinking of a number from 1 to 100.")
while true do
  io.write("your guess: "); io.flush()
  local line = io.read("l")
  if not line then break end
  local g = tonumber(line)
  if not g then print("type a number")
  else
    tries = tries + 1
    if g < secret then print("higher") elseif g > secret then print("lower")
    else print("Yes! " .. secret .. " in " .. tries .. " tries.") break end
  end
end
