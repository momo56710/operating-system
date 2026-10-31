--@ title: Units
--@ about: length, mass, temperature
local f = {
  km_mi = function(v) return v * 0.621371 end, mi_km = function(v) return v / 0.621371 end,
  kg_lb = function(v) return v * 2.20462 end, lb_kg = function(v) return v / 2.20462 end,
  c_f = function(v) return v * 9 / 5 + 32 end, f_c = function(v) return (v - 32) * 5 / 9 end,
  m_ft = function(v) return v * 3.28084 end, ft_m = function(v) return v / 3.28084 end,
}
print("type: VALUE UNIT_TO_UNIT, e.g.  100 km_mi    37 c_f    available: " .. (function() local k = {} for n in pairs(f) do k[#k + 1] = n end table.sort(k) return table.concat(k, " ") end)())
for line in io.lines() do
  local v, u = line:match("^(%S+)%s+(%S+)$")
  if v and f[u] and tonumber(v) then print(("%g %s = %.3f"):format(tonumber(v), u:gsub("_", " -> "), f[u](tonumber(v)))) else print("?") end
  io.write(""); io.flush()
end
