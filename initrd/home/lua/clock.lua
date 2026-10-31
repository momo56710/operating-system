--@ title: Text clock
--@ about: a digital clock in text
for i = 1, 15 do
  io.write("\r", os.date("!%H:%M:%S UTC"), "  (", 15 - i, " ticks left)  "); io.flush()
  leos.sleep(1000)
end
print()
