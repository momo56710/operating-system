--@ title: Tree
--@ about: draw a folder as a tree
--@ args: Folder (blank = /)
local function walk(dir, prefix, depth)
  local entries = leos.dir(dir)
  if not entries then return end
  table.sort(entries, function(a, b) return a.name < b.name end)
  local list = {}
  for _, e in ipairs(entries) do if e.name ~= "." and e.name ~= ".." then list[#list + 1] = e end end
  for i, e in ipairs(list) do
    local last = i == #list
    print(prefix .. (last and "`-- " or "|-- ") .. e.name .. (e.type == "dir" and "/" or ""))
    if e.type == "dir" and depth < 4 then walk((dir == "/" and "" or dir) .. "/" .. e.name, prefix .. (last and "    " or "|   "), depth + 1) end
  end
end
local root = arg[1] or "/"
print(root)
walk(root, "", 1)
