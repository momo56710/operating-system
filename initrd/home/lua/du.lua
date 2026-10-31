--@ title: Disk usage
--@ about: how big is each folder
--@ args: Folder (blank = /)
local function size(dir)
  local total = 0
  for _, e in ipairs(leos.dir(dir) or {}) do
    if e.name ~= "." and e.name ~= ".." then
      if e.type == "dir" then total = total + size((dir == "/" and "" or dir) .. "/" .. e.name) else total = total + e.size end
    end
  end
  return total
end
local root = arg[1] or "/"
for _, e in ipairs(leos.dir(root) or {}) do
  if e.type == "dir" and e.name ~= "." and e.name ~= ".." then print(("%10d  %s"):format(size((root == "/" and "" or root) .. "/" .. e.name), e.name .. "/")) end
end
print(("%10d  total"):format(size(root)))
