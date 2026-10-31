-- The LeOS desktop: a graphical shell styled after https://momodev.cc
-- (black, white monospace text, thin rounded borders, underlined links).
-- Run it with:  lua /home/desktop/desktop.lua      (or the alias `desktop`)
local ROOT = "/home/desktop/"
local W, H = gfx.open()

-- ---------------------------------------------------------------- theme and settings

local C = { bg = 0x000000, fg = 0xffffff, dim = 0x9ca3af, mute = 0x6b7280, line = 0xd1d5db, faint = 0x1f2937, dark = 0x0b0b0b }
local ACCENTS = { { "white", 0xffffff }, { "green", 0x4ade80 }, { "amber", 0xfbbf24 }, { "cyan", 0x22d3ee }, { "pink", 0xf472b6 } }
local WALLS = { "plain", "dots", "grid" }
local cfg = { accent = 1, wall = 2, cat = true, h24 = true, tz = 1 }
local function has_disk() return leos.stat("/disk/bin") ~= nil end      -- /disk is only an empty folder unless a FAT32 disk is mounted there
local CFG_PATH = "/disk/desktop.cfg"

local function save_cfg()
  if not has_disk() then return false end
  local f = io.open(CFG_PATH, "w")
  if not f then return false end
  for k, v in pairs(cfg) do f:write(k, "=", tostring(v), "\n") end
  f:close()
  return true
end

do
  local f = io.open(CFG_PATH, "r")
  if f then
    for line in f:lines() do
      local k, v = line:match("^(%w+)=(.+)$")
      if k and cfg[k] ~= nil then
        if v == "true" then cfg[k] = true elseif v == "false" then cfg[k] = false else cfg[k] = tonumber(v) or cfg[k] end
      end
    end
    f:close()
  end
end

local function accent() return ACCENTS[cfg.accent][2] end

-- ---------------------------------------------------------------- drawing helpers

local function text(x, y, s, c, sc) return gfx.text(x, y, s, c or C.fg, sc or 1) end
local function tw(s, sc) return #s * 8 * (sc or 1) end

local function wrap(s, maxc)
  local out = {}
  for para in (s .. "\n"):gmatch("(.-)\n") do
    local line = ""
    for word in para:gmatch("%S+") do
      if #line + #word + (line == "" and 0 or 1) > maxc and line ~= "" then out[#out + 1] = line; line = word
      else line = (line == "" and "" or line .. " ") .. word end
    end
    out[#out + 1] = line
  end
  return out
end

local function clock_str()
  local t = os.time() + math.floor(cfg.tz * 3600)
  local ok, s = pcall(os.date, cfg.h24 and "!%H:%M" or "!%I:%M %p", t)
  return ok and s or "--:--"
end

local function fmt_size(n)
  if n < 1024 then return n .. " B" end
  if n < 1048576 then return string.format("%.1f KB", n / 1024) end
  return string.format("%.1f MB", n / 1048576)
end

-- ---------------------------------------------------------------- icons (drawn from shapes, 56x56 box)

local function circle(cx, cy, r, c, filled)
  if filled then gfx.round(cx - r, cy - r, 2 * r, 2 * r, r, c) else gfx.frame(cx - r, cy - r, 2 * r, 2 * r, c, r) end
end

local ICONS = {
  about = function(x, y, c)
    circle(x + 28, y + 19, 8, c, true)
    gfx.round(x + 14, y + 31, 28, 16, 8, c)
    gfx.rect(x + 14, y + 40, 28, 7, C.bg)
  end,
  skills = function(x, y, c) text(x + 4, y + 12, "</>", c, 2) end,
  experience = function(x, y, c)
    gfx.frame(x + 14, y + 14, 28, 6, c, 2)
    gfx.frame(x + 8, y + 20, 40, 26, c, 4)
    gfx.rect(x + 8, y + 31, 40, 1, c)
    gfx.rect(x + 25, y + 29, 6, 5, c)
  end,
  projects = function(x, y, c)
    for i = 0, 1 do for j = 0, 1 do gfx.frame(x + 10 + i * 20, y + 10 + j * 20, 16, 16, c, 3) end end
    gfx.round(x + 14, y + 14, 8, 8, 2, c)
  end,
  contact = function(x, y, c)
    gfx.frame(x + 8, y + 14, 40, 28, c, 4)
    gfx.line(x + 9, y + 16, x + 28, y + 30, c)
    gfx.line(x + 47, y + 16, x + 28, y + 30, c)
  end,
  terminal = function(x, y, c)
    gfx.frame(x + 6, y + 10, 44, 36, c, 4)
    text(x + 12, y + 14, ">_", c, 2)
  end,
  files = function(x, y, c)
    gfx.frame(x + 8, y + 12, 18, 8, c, 3)
    gfx.frame(x + 8, y + 18, 40, 26, c, 4)
  end,
  notes = function(x, y, c)
    gfx.frame(x + 12, y + 8, 32, 40, c, 4)
    for i = 0, 3 do gfx.rect(x + 18, y + 16 + i * 8, 20 - (i == 3 and 8 or 0), 1, c) end
  end,
  settings = function(x, y, c)
    for i = 0, 7 do
      local a = i * math.pi / 4
      local px, py = x + 28 + math.floor(math.cos(a) * 16 + 0.5), y + 28 + math.floor(math.sin(a) * 16 + 0.5)
      gfx.rect(px - 3, py - 3, 6, 6, c)
    end
    circle(x + 28, y + 28, 12, c, true)
    circle(x + 28, y + 28, 5, C.bg, true)
  end,
  monitor = function(x, y, c)
    gfx.frame(x + 6, y + 10, 44, 32, c, 4)
    local pts = { 0, 22, 8, 18, 14, 28, 22, 14, 30, 24, 38, 16 }
    for i = 1, #pts - 2, 2 do gfx.line(x + 10 + pts[i], y + 8 + pts[i + 1], x + 10 + pts[i + 2], y + 8 + pts[i + 3], c) end
    gfx.rect(x + 20, y + 44, 16, 2, c)
  end,
}

-- ---------------------------------------------------------------- state

local imgs = {}
for _, n in ipairs({ "me", "cat0", "cat1", "cat2", "cat3" }) do imgs[n] = gfx.loadimage(ROOT .. "assets/" .. n .. ".img") end

local wins, regions = {}, {}
local mx, my = W // 2, H // 2
local down, drag, hoverkey = false, nil, nil
local toast_msg, toast_until = nil, 0
local running = true
local app_defs = {}
local started = leos.uptime()

local function now() return leos.uptime() end
local function toast(msg) toast_msg = msg; toast_until = now() + 2600 end
local function region(x, y, w, h, fn, key) regions[#regions + 1] = { x = x, y = y, w = w, h = h, fn = fn, key = key } end

-- ---------------------------------------------------------------- layout helpers for window content

local function L_begin(win, x, y, w) return { win = win, x = x, x0 = x, y = y - win.scroll, top = y - win.scroll, w = w } end

local function L_h(c, s, sc, col)
  text(c.x, c.y, s, col or C.fg, sc)
  c.y = c.y + 16 * sc + 10
end

local function L_p(c, s, col)
  for _, line in ipairs(wrap(s, c.w // 8)) do text(c.x, c.y, line, col or C.fg); c.y = c.y + 20 end
  c.y = c.y + 8
end

local function L_gap(c, n) c.y = c.y + n end

local function L_link(c, label, url, sc)
  sc = sc or 1
  local w = text(c.x, c.y, label, C.fg, sc)
  gfx.rect(c.x, c.y + 15 * sc, w, 1, C.fg)
  region(c.x, c.y, w, 16 * sc, function() toast(url) end, "link:" .. label)
  c.y = c.y + 16 * sc + 12
end

local function L_chips(c, list)
  local x, y = c.x, c.y
  for _, s in ipairs(list) do
    local w = #s * 8 + 20
    if x + w > c.x + c.w then x = c.x; y = y + 32 end
    gfx.frame(x, y, w, 24, C.line, 12)
    text(x + 10, y + 4, s, C.fg)
    x = x + w + 8
  end
  c.y = y + 24 + 12
end

local function L_end(c) c.win.content_h = c.y - c.top + 32 end

-- ---------------------------------------------------------------- portfolio content

local SKILLS = {
  { "Development", {
    { "Frontend", { "HTML", "CSS", "JavaScript", "TypeScript", "React", "Next.js" } },
    { "Backend", { "Node.js", "Express.js", "MongoDB", "PostgreSQL", "Firebase", "Django" } },
    { "DevOps & Infrastructure", { "Docker", "Nginx", "OpenSSH", "Ubuntu", "Arch Linux", "Domain Config", "Git & GitHub" } } } },
  { "AI & Tools", { { nil, { "Local Models", "Claude Code", "API Integration", "Prompt Engineering", "Gemini / Bard", "ChatGPT" } } } },
  { "Design & Video", { { nil, { "Figma", "Illustrator", "Photoshop", "After Effects", "Premiere Pro", "DaVinci Resolve", "Blender" } } } },
  { "Management & Leadership", { { nil, { "Project Management", "Agile / Scrum", "Team Leadership", "Public Speaking", "Client Communication", "Trello", "Notion" } } } },
  { "Languages", { { nil, { "Arabic: Native", "English: Fluent", "French: Conversational" } } } },
}

local JOBS = {
  { "ICT Teacher", "BIS", "Sep 2026 to Present", "Teaching ICT and computer science to secondary students." },
  { "IT Consulting Intern, SAP S/4HANA", "PwC", "Feb 2026 to Jul 2026", "Solved 10 client technical challenges, moving legacy processes to SAP Fiori." },
  { "Web Developer", "KIG", "Jun 2024 to Mar 2026", "Built React web apps in an Agile team of 5." },
  { "Tech Content Creator", "Self-Initiated", "Sep 2025 to Present", "Web dev and UI/UX tutorials reaching 10,000+ learners." },
  { "Web Development Mentor", "brainerX", "Mar 2025 to Sep 2025", "Trained 100+ students in frontend and backend development." },
  { "Intern Web Developer", "Startup Ministry", "Apr 2023 to May 2023", "Shipped 8 frontend features for an ERP system." },
  { "IT Team Lead", "Google Developer Group (GDG)", "Jun 2021 to Apr 2024", "Led 10+ events and workshops with 1,000+ total attendees." },
}

local PROJECTS = {
  { "MOMO mentorships", "Join the Mentorship", "study.momodev.cc" },
  { "twitch chat generator", "Try the Chat Generator", "twitch.momodev.cc" },
  { "Logframe AI Dashboard", "Explore AI Dashboard", "logframe.momodev.cc" },
  { "Linktify", "Try Linktify", "linktify.vercel.app" },
  { "ADFORS", "Discover ADFORS", "adfors.vercel.app/fr" },
  { "School Security System", "View Demo", "yacine-ui.vercel.app" },
  { "Loop Studios Landing Page", "See Landing Page", "loopstudios-momo56710.vercel.app" },
  { "Rock Paper Scissor", "Play Now", "rock-paper-sissor-five.vercel.app" },
  { "Multi Step Form", "Try the Form", "multi-steps-form-three.vercel.app" },
}

local SOCIALS = {
  { "gh", "GitHub", "github.com/momo56710" },
  { "in", "LinkedIn", "linkedin.com/in/momodev" },
  { "ig", "Instagram", "instagram.com/momodev.cc" },
}

-- ---------------------------------------------------------------- launchers: every Lua script and window app is an icon

package.path = ROOT .. "?.lua;" .. package.path
local emu = require("emu")

local open_app, close_win, open_script, scan_launchers, load_app_def

local SCRIPT_DIRS = { "/home/lua", "/disk/lua" }
local APP_DIRS = { "/home/desktop/apps", "/disk/apps" }
local launchers = {}

-- A script describes itself in its first lines:  --@ title: Primes   --@ about: ...   --@ args: prompt for arguments
local function read_header(path)
  local meta = {}
  local f = io.open(path, "r")
  if not f then return meta end
  for _ = 1, 12 do
    local line = f:read("l")
    if not line then break end
    local k, v = line:match("^%-%-@%s*(%w+)%s*:%s*(.-)%s*$")
    if k then meta[k] = v end
  end
  f:close()
  return meta
end

scan_launchers = function()
  launchers = {}
  local function add(kind, dirs)
    for _, dir in ipairs(dirs) do
      for _, e in ipairs(leos.dir(dir) or {}) do
        if e.type ~= "dir" and e.name:match("%.lua$") then
          local path = dir .. "/" .. e.name
          local m = read_header(path)
          local base = e.name:gsub("%.lua$", "")
          launchers[#launchers + 1] = { kind = kind, path = path, title = m.title or base, about = m.about or path, args = m.args }
        end
      end
    end
  end
  add("app", APP_DIRS)
  add("script", SCRIPT_DIRS)
  table.sort(launchers, function(a, b)
    if a.kind ~= b.kind then return a.kind == "app" end
    return a.title:lower() < b.title:lower()
  end)
end

-- what a window app can use (the same helpers the built-in apps use)
local ui = { text = text, tw = tw, region = region, toast = toast, wrap = wrap, accent = accent, C = C, now = now, fmt_size = fmt_size,
             circle = circle, clock = clock_str, W = function() return W end, H = function() return H end }

load_app_def = function(path)
  local chunk, err = loadfile(path)
  if not chunk then return nil, err end
  local ok, def = pcall(chunk, ui)
  if not ok then return nil, tostring(def) end
  if type(def) ~= "table" or type(def.draw) ~= "function" then return nil, "an app must return a table with a draw function" end
  def.w, def.h, def.title = def.w or 480, def.h or 360, def.title or path:match("([^/]+)%.lua$")
  return def
end

-- ---- running a script: a real process, its output streamed into the window

local function split_args(s)
  local t = {}
  for w in (s or ""):gmatch("%S+") do t[#t + 1] = w end
  return t
end

local function proc_start(win, ...)
  local p, err = leos.spawn(...)
  if not p then
    win.emu:feed("cannot start: " .. tostring(err) .. "\n")
    win.state, win.exit = "exited", -1
    return false
  end
  win.proc, win.state, win.exit, win.vs, win.eof = p, "running", nil, 0, false
  win.t0 = now()
  return true
end

local function proc_stop(win)
  if win.proc then
    win.proc:kill()
    win.proc:closeinput()        -- a program waiting for input sees the end of the file and notices it was killed
    win.proc:close()             -- ... and one blocked writing finds the pipe closed
  end
end

local function run_start(win)
  win.emu:clear()
  proc_start(win, "/bin/lua", win.script, table.unpack(split_args(win.argline)))
end

local function drain(win)
  local got = false
  for _ = 1, 4 do
    local s = win.proc:read()
    if s == nil then win.eof = true break end
    if s == "" then break end
    win.emu:feed(s)
    got = true
  end
  return got
end

-- called every loop: move output into the windows and notice finished programs. true if anything changed
local function pump()
  local changed = false
  for _, win in ipairs(wins) do
    if win.proc and not win.eof and drain(win) then changed = true; win.vs = 0 end
  end
  local pid, status = leos.reap()
  while pid do
    for _, win in ipairs(wins) do
      if win.proc and win.proc:pid() == pid then
        while not win.eof and drain(win) do end
        win.proc:close(); win.proc = nil
        win.state, win.exit, win.vs = "exited", status, 0
        changed = true
      end
    end
    pid, status = leos.reap()
  end
  return changed
end

open_script = function(l, args)
  local win = open_app("run", true)
  win.title, win.script, win.emu, win.input, win.hint = l.title, l.path, emu.new(80), "", l.args
  if l.args and not args then win.state = "ask"
  else win.argline = args or ""; run_start(win) end
  return win
end

local function launch(l)
  if l.kind == "script" then open_script(l) return end
  local key = "app:" .. l.path
  if not app_defs[key] then
    local def, err = load_app_def(l.path)
    if not def then toast("cannot load " .. l.title .. ": " .. tostring(err):sub(1, 60)) return end
    app_defs[key] = def
  end
  open_app(key)
end

-- ---------------------------------------------------------------- apps

app_defs.about = {
  title = "about", w = 720, h = 500, icon = "about",
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    local textw = w - 40 - 232
    if textw < 240 then textw = w - 40 end
    local ty = c.y
    c.w = textw
    L_h(c, "About me", 2)
    L_p(c, "I'm Mohamed Amine Metidji, known online as momodev. I'm an ICT teacher at BIS, a web developer and a dev mentor. I've been coding since 2017 and I help beginners learn web development.")
    L_gap(c, 12)
    L_link(c, "Resume mentor", "momodev.cc/Resume-metidji-mohamed-amine.pdf", 2)
    L_link(c, "Resume web developer", "momodev.cc/Resume-metidji-mohamed-amine-webdev.pdf", 2)
    L_link(c, "Resume consultant", "momodev.cc/Resume-metidji-mohamed-amine-consulting.pdf", 2)
    if textw < w - 40 then
      gfx.image(imgs.me, x + w - 20 - 200, ty, 200, 200)
      gfx.frame(x + w - 20 - 200, ty, 200, 200, C.line, 0)
    end
    L_end(c)
  end,
}

app_defs.skills = {
  title = "skills", w = 700, h = 520, icon = "skills",
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    L_h(c, "My Skills", 2)
    for _, cat in ipairs(SKILLS) do
      L_h(c, cat[1], 1, C.fg)
      for _, grp in ipairs(cat[2]) do
        if grp[1] then text(c.x + 8, c.y, grp[1], C.dim); c.y = c.y + 24 end
        L_chips(c, grp[2])
      end
      L_gap(c, 6)
    end
    L_end(c)
  end,
}

app_defs.experience = {
  title = "experience", w = 700, h = 540, icon = "experience",
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    L_h(c, "Experience", 2)
    for _, j in ipairs(JOBS) do
      local lines = wrap(j[4], (c.w - 20) // 8)
      local bh = 20 + 22 + #lines * 20 + 6
      gfx.rect(c.x, c.y, 1, bh, C.line)
      text(c.x + 16, c.y, j[1], C.fg)
      text(c.x + 16, c.y + 22, j[2] .. "  .  " .. j[3], C.mute)
      for i, line in ipairs(lines) do text(c.x + 16, c.y + 44 + (i - 1) * 20, line, C.dim) end
      c.y = c.y + bh + 18
    end
    L_end(c)
  end,
}

app_defs.projects = {
  title = "projects", w = 700, h = 520, icon = "projects",
  init = function(win) win.open = {} end,
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    L_h(c, "My Projects", 2)
    for i, p in ipairs(PROJECTS) do
      local isopen = win.open[i]
      local rowy = c.y
      text(c.x, rowy, p[1], C.fg, 1)
      text(c.x + c.w - 16, rowy, isopen and "^" or "v", C.dim)
      region(c.x, rowy, c.w, 24, function() win.open[i] = not win.open[i] end, "proj" .. i)
      c.y = rowy + 30
      if isopen then
        local lw = text(c.x + 16, c.y, "-> " .. p[2], accent())
        gfx.rect(c.x + 16, c.y + 15, lw, 1, accent())
        region(c.x + 16, c.y, lw, 16, function() toast(p[3]) end, "plink" .. i)
        c.y = c.y + 30
      end
      gfx.rect(c.x, c.y - 4, c.w, 1, C.faint)
    end
    L_end(c)
  end,
}

app_defs.contact = {
  title = "contact", w = 640, h = 420, icon = "contact",
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    L_h(c, "Let's Talk", 2)
    text(c.x, c.y, "phone : ", C.dim); text(c.x + 64, c.y, "+213562519164", C.fg); c.y = c.y + 26
    text(c.x, c.y, "mail : ", C.dim); text(c.x + 56, c.y, "metidjimohamedamine8@gmail.com", C.fg); c.y = c.y + 26
    text(c.x, c.y, "location : ", C.dim); text(c.x + 88, c.y, "Medea, Medea, algeria", C.fg); c.y = c.y + 40
    for _, s in ipairs(SOCIALS) do
      gfx.frame(c.x, c.y, 40, 40, C.line, 8)
      text(c.x + 12, c.y + 12, s[1], C.fg)
      local lw = text(c.x + 56, c.y + 12, s[2], C.fg)
      gfx.rect(c.x + 56, c.y + 27, lw, 1, C.fg)
      region(c.x, c.y, 56 + lw, 40, function() toast(s[3]) end, "soc" .. s[1])
      c.y = c.y + 52
    end
    L_end(c)
  end,
}

-- ---- shared by the terminal and the script runner: draw an emulated screen

local function draw_screen(win, x, y, w, rows, tail)
  win.emu.cols = (w - 24) // 8
  local lines = win.emu.lines
  local extra = tail and 1 or 0                         -- a prompt line the terminal adds below the output
  local total = #lines + extra
  local last = total - (win.vs or 0)
  local first = math.max(1, last - rows + 1)
  for i = first, last do
    local l = i <= #lines and lines[i] or tail
    if i == #lines and extra == 0 and tail == nil then l = lines[i] end
    text(x + 12, y + (i - first) * 18, l:sub(1, win.emu.cols + 8), C.fg)
  end
  if (win.vs or 0) > 0 then text(x + w - 120, y, "-" .. win.vs .. " lines", C.mute) end
end

-- ---- terminal: a real command line. Programs run as processes; pipes, redirects and scripts all work.

local function normalize(cwd, p)
  local parts = {}
  local full = p:sub(1, 1) == "/" and p or (cwd .. "/" .. p)
  for seg in full:gmatch("[^/]+") do
    if seg == ".." then parts[#parts] = nil elseif seg ~= "." then parts[#parts + 1] = seg end
  end
  return "/" .. table.concat(parts, "/")
end

local TERM_HELP = [[
Everything you type runs through the LeOS shell: ls, cat, grep, wc, ps, mem, ping, wget...
pipes and redirects work, and so do Lua scripts:  primes.lua  or  lua /home/lua/life.lua
built-ins:  cd  clear  help  exit        =EXPR evaluates a Lua expression, e.g.  =2^10
Ctrl+C stops the running program, Ctrl+D ends its input, PageUp/PageDown scroll.
(Full-screen programs such as nano need a real console: use the notes app instead.)]]

local function term_prompt(win) return win.cwd .. " $ " end

local function term_submit(win, line)
  win.emu:feed((win.proc and "" or term_prompt(win)) .. line .. "\n")
  if win.proc then win.proc:write(line .. "\n") return end
  if line:match("^%s*$") then return end
  if win.hist[#win.hist] ~= line then win.hist[#win.hist + 1] = line end
  win.hi = 0
  local cmd, rest = line:match("^%s*(%S+)%s*(.-)%s*$")
  if cmd == "clear" then win.emu:clear()
  elseif cmd == "exit" then close_win(win)
  elseif cmd == "help" then win.emu:feed(TERM_HELP .. "\n")
  elseif cmd == "cd" then
    local target = normalize(win.cwd, rest == "" and "/" or rest)
    local st = leos.stat(target)
    if st and st.type == "dir" then win.cwd = target else win.emu:feed("cd: no such directory: " .. target .. "\n") end
  elseif cmd:sub(1, 1) == "=" then
    local expr = line:match("^%s*=(.*)$")
    local f, err = load("return " .. expr, "=lua", "t", setmetatable({}, { __index = _G }))
    if not f then win.emu:feed(tostring(err) .. "\n") else
      local ok, v = pcall(f)
      win.emu:feed(tostring(v) .. "\n")
    end
  else
    leos.chdir(win.cwd)
    proc_start(win, "/bin/sh", "-c", line)
  end
end

app_defs.terminal = {
  title = "terminal", w = 780, h = 480, icon = "terminal", plain = true,
  init = function(win)
    win.emu = emu.new(90)
    win.emu:feed("LeOS terminal. Type help, or try:  ls /bin   primes.lua   ps | grep lua\n")
    win.cwd, win.input, win.hist, win.hi, win.vs = "/", "", {}, 0, 0
  end,
  draw = function(win, x, y, w, h)
    local rows = (h - 16) // 18
    local caret = (now() // 500) % 2 == 0 and "_" or " "
    if win.proc then
      local lines = win.emu.lines
      local keep = lines[#lines]
      lines[#lines] = keep .. win.input .. caret          -- what is being typed shows at the end of the program's last line
      draw_screen(win, x, y + 8, w, rows)
      lines[#lines] = keep
    else
      draw_screen(win, x, y + 8, w, rows, term_prompt(win) .. win.input .. caret)
    end
    if win.state == "running" then
      gfx.frame(x + w - 76, y + 4, 64, 22, hoverkey == "tstop" and C.fg or C.mute, 6)
      text(x + w - 64, y + 7, "stop", C.dim)
      region(x + w - 76, y + 4, 64, 22, function() proc_stop(win) end, "tstop")
    end
  end,
  key = function(win, ev)
    if not ev.down then return end
    local c = ev.code
    if (ev.mods & 2) ~= 0 then
      if ev.ch == "c" then if win.proc then proc_stop(win) end win.input = ""
      elseif ev.ch == "d" and win.proc then win.proc:closeinput()
      elseif ev.ch == "l" then win.emu:clear() end
      return
    end
    if c == 0x1C then local line = win.input; win.input = ""; term_submit(win, line)
    elseif c == 0x0E then win.input = win.input:sub(1, -2)
    elseif c == 0x149 then win.vs = math.min(#win.emu.lines - 1, win.vs + 10)
    elseif c == 0x151 then win.vs = math.max(0, win.vs - 10)
    elseif c == 0x148 and not win.proc and #win.hist > 0 then win.hi = math.min(#win.hist, win.hi + 1); win.input = win.hist[#win.hist - win.hi + 1]
    elseif c == 0x150 and not win.proc then win.hi = math.max(0, win.hi - 1); win.input = win.hi > 0 and win.hist[#win.hist - win.hi + 1] or ""
    elseif c == 0x0F and not win.proc then                  -- tab: complete a file name in the current folder
      local word = win.input:match("(%S*)$")
      if word ~= "" then
        local dir, partial = word:match("^(.*/)([^/]*)$")
        dir, partial = dir or "", partial or word
        for _, e in ipairs(leos.dir(normalize(win.cwd, dir == "" and "." or dir)) or {}) do
          if e.name:sub(1, #partial) == partial and e.name ~= "." and e.name ~= ".." then
            win.input = win.input:sub(1, #win.input - #word) .. dir .. e.name .. (e.type == "dir" and "/" or "")
            break
          end
        end
      end
    elseif ev.ch and ev.ch:byte() >= 32 then win.input = win.input .. ev.ch end
  end,
  close = function(win) proc_stop(win) end,
}

-- ---- script runner window: opened by a script icon

app_defs.run = {
  title = "run", w = 700, h = 460, icon = "terminal", plain = true,
  init = function(win) win.vs, win.input, win.state = 0, "", "exited" end,
  draw = function(win, x, y, w, h)
    local running = win.state == "running"
    local bx = x + w - 12
    local function button(label, fn, key)
      local bw = tw(label) + 20
      bx = bx - bw
      gfx.frame(bx, y + 6, bw, 24, hoverkey == key and C.fg or C.line, 8)
      text(bx + 10, y + 10, label, C.fg)
      region(bx, y + 6, bw, 24, fn, key)
      bx = bx - 8
    end
    if running then button("stop", function() proc_stop(win) end, "rstop")
    elseif win.state == "exited" then button("run again", function() win.state = win.hint and "ask" or "exited"; if not win.hint then run_start(win) end end, "rrerun") end
    button("clear", function() win.emu:clear() end, "rclear")
    local status = running and "running..." or win.state == "ask" and "waiting for arguments" or ("finished, exit code " .. tostring(win.exit))
    if win.state == "exited" and win.exit == 137 then status = "stopped" end
    text(x + 12, y + 10, (win.title .. "  -  " .. status):sub(1, (bx - x) // 8), (win.state == "exited" and win.exit ~= 0) and C.dim or accent())
    gfx.rect(x, y + 36, w, 1, C.faint)
    local rows = (h - 36 - 44) // 18
    if win.state == "ask" then
      text(x + 12, y + 50, win.hint .. ":", C.dim)
      text(x + 12, y + 74, "(press Enter to start; leave it blank for the default)", C.mute)
    else
      draw_screen(win, x, y + 44, w, rows)
    end
    gfx.rect(x, y + h - 36, w, 1, C.faint)
    local label = win.state == "ask" and "args> " or running and "input> " or "      "
    local caret = (now() // 500) % 2 == 0 and "_" or " "
    text(x + 12, y + h - 28, label .. win.input .. ((win.state == "ask" or running) and caret or ""), C.fg)
  end,
  key = function(win, ev)
    if not ev.down then return end
    local c = ev.code
    if (ev.mods & 2) ~= 0 then
      if ev.ch == "c" and win.proc then proc_stop(win)
      elseif ev.ch == "d" and win.proc then win.proc:closeinput() end
      return
    end
    if c == 0x1C then
      if win.state == "ask" then win.argline = win.input; win.input = ""; run_start(win)
      elseif win.state == "running" and win.proc then
        win.emu:feed(win.input .. "\n"); win.proc:write(win.input .. "\n"); win.input = ""
      end
    elseif c == 0x0E then win.input = win.input:sub(1, -2)
    elseif c == 0x149 or c == 0x148 then win.vs = math.min(#win.emu.lines - 1, win.vs + (c == 0x149 and 10 or 1))
    elseif c == 0x151 or c == 0x150 then win.vs = math.max(0, win.vs - (c == 0x151 and 10 or 1))
    elseif ev.ch and ev.ch:byte() >= 32 then win.input = win.input .. ev.ch end
  end,
  close = function(win) proc_stop(win) end,
}

-- ---- notes (text editor; also a place to write and run Lua scripts)

local function notes_default_path() return has_disk() and "/disk/notes.txt" or "/tmp/notes.txt" end

local function notes_load(win, path)
  win.path, win.lines, win.cx, win.cy, win.dirty, win.top0, win.pathedit = path, {}, 1, 1, false, 0, nil
  local f = io.open(path, "r")
  if f then for line in f:lines() do win.lines[#win.lines + 1] = line end f:close() end
  if #win.lines == 0 then win.lines[1] = "" end
end

local function in_script_dir(path)
  for _, d in ipairs(SCRIPT_DIRS) do if path:sub(1, #d + 1) == d .. "/" then return true end end
  return false
end

local function notes_save(win)
  local dir = win.path:match("^(.*)/[^/]*$")
  if dir and dir ~= "" and not leos.exists(dir) then leos.mkdir(dir) end
  local f = io.open(win.path, "w")
  if f then
    for _, l in ipairs(win.lines) do f:write(l, "\n") end
    f:close(); win.dirty = false; toast("saved " .. win.path)
    if win.path:match("%.lua$") and in_script_dir(win.path) then scan_launchers() end     -- a new script becomes an icon
    return true
  end
  toast("cannot save " .. win.path)
end

local function notes_run(win)
  if not win.path:match("%.lua$") then toast("only .lua files can run: use  save as  with a name ending in .lua") return end
  if win.dirty and not notes_save(win) then return end
  local title = win.path:match("([^/]+)%.lua$")
  open_script({ title = title, path = win.path, about = win.path }, "")
end

app_defs.notes = {
  title = "notes", w = 680, h = 460, icon = "notes", plain = true,
  init = function(win) notes_load(win, notes_default_path()) end,
  draw = function(win, x, y, w, h)
    local label = (win.pathedit and ("save as: " .. win.pathedit .. ((now() // 500) % 2 == 0 and "_" or " ")) or (win.path .. (win.dirty and " *" or "")))
    text(x + 12, y + 8, label:sub(1, (w - 230) // 8), win.pathedit and C.fg or C.dim)
    if not win.pathedit then region(x + 8, y + 4, math.min(#label * 8 + 8, w - 230), 24, function() win.pathedit = win.path end, "rename") end
    local bx = x + w - 12
    for _, b in ipairs({ { "run", function() notes_run(win) end }, { "save", function() notes_save(win) end }, { "new", function() notes_load(win, "/tmp/untitled.lua"); win.lines = { "" }; win.cx, win.cy, win.dirty = 1, 1, true; win.pathedit = (has_disk() and "/disk/lua/" or "/tmp/") .. "untitled.lua" end } }) do
      local bw = tw(b[1]) + 20
      bx = bx - bw
      gfx.frame(bx, y + 4, bw, 22, hoverkey == "n" .. b[1] and C.fg or C.line, 6)
      text(bx + 10, y + 7, b[1], C.fg)
      region(bx, y + 4, bw, 22, b[2], "n" .. b[1])
      bx = bx - 8
    end
    gfx.rect(x, y + 30, w, 1, C.faint)
    local rows = (h - 40) // 18
    if win.cy < win.top0 + 1 then win.top0 = win.cy - 1 end
    if win.cy > win.top0 + rows then win.top0 = win.cy - rows end
    local isLua = win.path:match("%.lua$")
    for i = 1, rows do
      local ln = win.lines[win.top0 + i]
      if not ln then break end
      local col = C.fg
      if isLua and ln:match("^%s*%-%-") then col = C.mute end
      text(x + 12, y + 36 + (i - 1) * 18, ln:sub(1, (w - 24) // 8), col)
    end
    if (now() // 500) % 2 == 0 and not win.pathedit then
      gfx.rect(x + 12 + (win.cx - 1) * 8, y + 36 + (win.cy - win.top0 - 1) * 18, 8, 16, accent())
    end
    text(x + 12, y + h - 22, "ctrl+s save   ctrl+r run   click the name to save as", C.mute)
  end,
  key = function(win, ev)
    if not ev.down then return end
    local c = ev.code
    if win.pathedit then
      if c == 0x1C then
        local p = win.pathedit; win.pathedit = nil
        if p:sub(1, 1) ~= "/" then p = "/tmp/" .. p end
        if leos.exists(p) and p ~= win.path then notes_load(win, p) else win.path = p; win.dirty = true end
      elseif c == 0x01 then win.pathedit = nil
      elseif c == 0x0E then win.pathedit = win.pathedit:sub(1, -2)
      elseif ev.ch and ev.ch:byte() >= 32 then win.pathedit = win.pathedit .. ev.ch end
      return
    end
    local ln = win.lines[win.cy]
    if (ev.mods & 2) ~= 0 then
      if ev.ch == "s" then notes_save(win) elseif ev.ch == "r" then notes_run(win) end
      return
    end
    if c == 0x1C then
      local indent = ln:match("^%s*")
      table.insert(win.lines, win.cy + 1, indent .. ln:sub(win.cx)); win.lines[win.cy] = ln:sub(1, win.cx - 1); win.cy = win.cy + 1; win.cx = #indent + 1; win.dirty = true
    elseif c == 0x0E then
      if win.cx > 1 then win.lines[win.cy] = ln:sub(1, win.cx - 2) .. ln:sub(win.cx); win.cx = win.cx - 1; win.dirty = true
      elseif win.cy > 1 then
        local prev = win.lines[win.cy - 1]
        win.lines[win.cy - 1] = prev .. ln; table.remove(win.lines, win.cy); win.cy = win.cy - 1; win.cx = #prev + 1; win.dirty = true
      end
    elseif c == 0x153 then
      if win.cx <= #ln then win.lines[win.cy] = ln:sub(1, win.cx - 1) .. ln:sub(win.cx + 1); win.dirty = true
      elseif win.lines[win.cy + 1] then win.lines[win.cy] = ln .. table.remove(win.lines, win.cy + 1); win.dirty = true end
    elseif c == 0x14B then if win.cx > 1 then win.cx = win.cx - 1 elseif win.cy > 1 then win.cy = win.cy - 1; win.cx = #win.lines[win.cy] + 1 end
    elseif c == 0x14D then if win.cx <= #ln then win.cx = win.cx + 1 elseif win.lines[win.cy + 1] then win.cy = win.cy + 1; win.cx = 1 end
    elseif c == 0x148 then if win.cy > 1 then win.cy = win.cy - 1; win.cx = math.min(win.cx, #win.lines[win.cy] + 1) end
    elseif c == 0x150 then if win.lines[win.cy + 1] then win.cy = win.cy + 1; win.cx = math.min(win.cx, #win.lines[win.cy] + 1) end
    elseif c == 0x149 then win.cy = math.max(1, win.cy - 15); win.cx = math.min(win.cx, #win.lines[win.cy] + 1)
    elseif c == 0x151 then win.cy = math.min(#win.lines, win.cy + 15); win.cx = math.min(win.cx, #win.lines[win.cy] + 1)
    elseif c == 0x147 then win.cx = 1
    elseif c == 0x14F then win.cx = #ln + 1
    elseif c == 0x0F then win.lines[win.cy] = ln:sub(1, win.cx - 1) .. "  " .. ln:sub(win.cx); win.cx = win.cx + 2; win.dirty = true
    elseif ev.ch and ev.ch:byte() >= 32 then win.lines[win.cy] = ln:sub(1, win.cx - 1) .. ev.ch .. ln:sub(win.cx); win.cx = win.cx + 1; win.dirty = true end
  end,
  click = function(win, mx, my)         -- click in the text: put the cursor there
    local row = (my - 36) // 18 + 1 + win.top0
    if my >= 36 and win.lines[row] then win.cy = row; win.cx = math.max(1, math.min(#win.lines[row] + 1, (mx - 12) // 8 + 1)) end
  end,
}

-- ---- files

app_defs.files = {
  title = "files", w = 640, h = 480, icon = "files", plain = true,
  init = function(win) win.path = "/"; win.scroll = 0 end,
  draw = function(win, x, y, w, h)
    text(x + 12, y + 8, win.path, C.fg)
    gfx.rect(x, y + 30, w, 1, C.faint)
    local d = leos.dir(win.path) or {}
    table.sort(d, function(a, b) if (a.type == "dir") ~= (b.type == "dir") then return a.type == "dir" end return a.name < b.name end)
    local rows = { { name = "..", type = "dir", size = 0 } }
    for _, e in ipairs(d) do if e.name ~= "." and e.name ~= ".." then rows[#rows + 1] = e end end
    local ry = y + 38 - win.scroll
    local base = win.path == "/" and "" or win.path
    for i, e in ipairs(rows) do
      if ry > y - 24 and ry < y + h then
        local hot = hoverkey == "file" .. i
        if hot then gfx.round(x + 6, ry - 2, w - 12, 22, 6, C.faint) end
        text(x + 14, ry, e.type == "dir" and "[+]" or " - ", C.dim)
        text(x + 52, ry, e.name .. (e.type == "dir" and "/" or ""), e.type == "dir" and C.fg or C.dim)
        if e.type ~= "dir" then text(x + w - 100, ry, fmt_size(e.size), C.mute) end
        region(x, ry - 2, w, 22, function()
          if e.type == "dir" then
            if e.name == ".." then win.path = win.path:match("^(.*)/[^/]*$") or "/"; if win.path == "" then win.path = "/" end
            else win.path = base .. "/" .. e.name end
            win.scroll = 0
          elseif e.size < 65536 then
            local n = open_app("notes", true)
            notes_load(n, base .. "/" .. e.name)
          else toast("file too large for notes") end
        end, "file" .. i)
        if e.type ~= "dir" and e.name:match("%.lua$") then
          local rx = x + w - 170
          gfx.frame(rx, ry - 1, 48, 20, hoverkey == "frun" .. i and C.fg or C.line, 6)
          text(rx + 8, ry, "run", hoverkey == "frun" .. i and accent() or C.fg)
          region(rx, ry - 1, 48, 20, function()
            open_script({ title = e.name:gsub("%.lua$", ""), path = base .. "/" .. e.name, about = e.name }, "")
          end, "frun" .. i)
        end
      end
      ry = ry + 24
    end
    win.content_h = #rows * 24 + 46
  end,
}

-- ---- system monitor

app_defs.monitor = {
  title = "monitor", w = 640, h = 480, icon = "monitor", plain = true,
  init = function(win) win.hist = {} end,
  draw = function(win, x, y, w, h)
    local m = leos.meminfo()
    local up = m.uptime_ms // 1000
    text(x + 16, y + 14, string.format("uptime  %02d:%02d:%02d", up // 3600, up // 60 % 60, up % 60), C.fg)
    local used = m.total_kb - m.free_kb
    local function bar(by, label, v, total)
      text(x + 16, by, label, C.dim)
      gfx.frame(x + 220, by - 2, w - 240, 20, C.line, 6)
      local fw = math.floor((w - 244) * v / total)
      if fw > 0 then gfx.round(x + 222, by, fw, 16, 4, accent()) end
    end
    bar(y + 48, string.format("memory %d/%d MiB", used // 1024, m.total_kb // 1024), used, m.total_kb)
    bar(y + 78, string.format("heap   %d KiB", m.heap_used // 1024), m.heap_used, 16 * 1024 * 1024)
    text(x + 16, y + 116, "pid  state     kind    name", C.mute)
    gfx.rect(x + 16, y + 134, w - 32, 1, C.faint)
    local ry = y + 142
    for _, t in ipairs(leos.ps()) do
      if ry > y + h - 20 then break end
      text(x + 16, ry, string.format("%-4d %-9s %-7s %s", t.pid, t.state, t.kind, t.name), C.fg)
      ry = ry + 18
    end
  end,
}

-- ---- settings

local function toggle_row(win, x, y, w, label, on, fn, key)
  text(x, y + 2, label, C.fg)
  gfx.frame(x + w - 56, y, 56, 24, C.line, 12)
  gfx.round(x + w - 54 + (on and 30 or 0), y + 2, 22, 20, 10, on and accent() or C.mute)
  region(x + w - 56, y, 56, 24, fn, key)
end

app_defs.settings = {
  title = "settings", w = 600, h = 560, icon = "settings",
  draw = function(win, x, y, w, h)
    local c = L_begin(win, x + 20, y + 16, w - 40)
    L_h(c, "Settings", 2)
    text(c.x, c.y, "accent colour", C.dim); c.y = c.y + 26
    for i, a in ipairs(ACCENTS) do
      local bx = c.x + (i - 1) * 56
      gfx.round(bx, c.y, 44, 28, 8, a[2])
      if cfg.accent == i then gfx.frame(bx - 4, c.y - 4, 52, 36, C.fg, 10) end
      region(bx, c.y, 44, 28, function() cfg.accent = i; save_cfg() end, "acc" .. i)
    end
    c.y = c.y + 52
    text(c.x, c.y, "wallpaper", C.dim); c.y = c.y + 26
    for i, name in ipairs(WALLS) do
      local bx = c.x + (i - 1) * 100
      gfx.frame(bx, c.y, 88, 28, cfg.wall == i and C.fg or C.mute, 8)
      text(bx + 44 - #name * 4, c.y + 6, name, cfg.wall == i and C.fg or C.dim)
      region(bx, c.y, 88, 28, function() cfg.wall = i; save_cfg() end, "wall" .. i)
    end
    c.y = c.y + 52
    toggle_row(win, c.x, c.y, c.w, "pixel cat", cfg.cat, function() cfg.cat = not cfg.cat; save_cfg() end, "cat"); c.y = c.y + 40
    toggle_row(win, c.x, c.y, c.w, "24 hour clock", cfg.h24, function() cfg.h24 = not cfg.h24; save_cfg() end, "h24"); c.y = c.y + 40
    text(c.x, c.y + 2, "time zone  UTC" .. (cfg.tz >= 0 and "+" or "") .. cfg.tz, C.fg)
    for k, d in ipairs({ -1, 1 }) do
      local bx = c.x + c.w - 56 + (k - 1) * 32
      gfx.frame(bx, c.y, 24, 24, C.line, 6)
      text(bx + 8, c.y + 4, d < 0 and "-" or "+", C.fg)
      region(bx, c.y, 24, 24, function() cfg.tz = math.max(-12, math.min(14, cfg.tz + d)); save_cfg() end, "tz" .. d)
    end
    c.y = c.y + 52
    gfx.rect(c.x, c.y, c.w, 1, C.faint); c.y = c.y + 16
    text(c.x, c.y, "about this system", C.dim); c.y = c.y + 26
    local m = leos.meminfo()
    local n = leos.netinfo()
    local info = {
      "LeOS 1.2 (x86_64)", W .. "x" .. H .. " framebuffer",
      string.format("%d MiB memory", m.total_kb // 1024),
      n and ("network " .. n.ip) or "no network",
      has_disk() and "disk mounted: settings are saved" or "no disk: settings last until shutdown",
    }
    for _, s in ipairs(info) do text(c.x, c.y, s, C.fg); c.y = c.y + 22 end
    L_end(c)
  end,
}

-- ---------------------------------------------------------------- windows

local capture
local power_dlg = false
local TITLE_H = 34
local MAIN_APPS = { "about", "skills", "experience", "projects", "contact", "terminal", "files", "notes", "settings", "monitor" }

local function top_win()
  for i = #wins, 1, -1 do if not wins[i].min then return wins[i] end end
end

local function focus(win)
  for i, w in ipairs(wins) do if w == win then table.remove(wins, i) break end end
  wins[#wins + 1] = win
  win.min = false
end

open_app = function(name, fresh)
  if not fresh then
    for _, w in ipairs(wins) do if w.name == name then focus(w) return w end end
  end
  local d = app_defs[name]
  local n = #wins
  local w, h = math.min(d.w, W - 40), math.min(d.h, H - 150)
  local win = { name = name, def = d, title = d.title, w = w, h = h, scroll = 0, content_h = 0, top0 = 0,
                x = math.min(W - w - 10, 250 + (n % 6) * 36), y = math.min(H - h - 80, 90 + (n % 6) * 30) }
  if d.init then d.init(win) end
  wins[#wins + 1] = win
  return win
end

close_win = function(win)
  if win.def.close then win.def.close(win) end
  for i, w in ipairs(wins) do if w == win then table.remove(wins, i) break end end
end

local function maximize(win)
  if win.restore then
    win.x, win.y, win.w, win.h = table.unpack(win.restore); win.restore = nil
  else
    win.restore = { win.x, win.y, win.w, win.h }
    win.x, win.y, win.w, win.h = 12, 60, W - 24, H - 60 - 70
  end
end

-- ---------------------------------------------------------------- scene

local typed_a = "Mohamed Amine Metidji"
local typed_b = "ICT Teacher & Web Developer"

local function draw_wall()
  gfx.rect(0, 0, W, H, C.bg)
  if cfg.wall == 2 then
    for yy = 40, H, 32 do for xx = 20, W, 32 do gfx.rect(xx, yy, 2, 2, C.faint) end end
  elseif cfg.wall == 3 then
    for yy = 0, H, 40 do gfx.rect(0, yy, W, 1, C.dark) end
    for xx = 0, W, 40 do gfx.rect(xx, 0, 1, H, C.dark) end
  end
end

local function draw_taskbar()
  gfx.frame(12, 10, W - 24, 40, C.line, 12)
  text(28, 21, "momo@leos", accent())
  text(28 + 80, 21, ":~$", C.fg)
  local x = 28 + 80 + 36
  local focused = top_win()
  for _, name in ipairs({ "about", "skills", "experience", "projects", "contact" }) do
    local w = tw(name)
    local active = focused and focused.name == name
    local col = active and C.dim or (hoverkey == "nav" .. name and C.fg or C.line)
    text(x, 21, name, col)
    gfx.rect(x, 21 + 15, w, 1, col)
    region(x - 4, 14, w + 8, 32, function() open_app(name) end, "nav" .. name)
    x = x + w + 24
  end
  -- far right: power button, then the clock, then "exit" (leave the desktop, back to the text shell)
  local pbx = W - 28 - 30
  local hot = hoverkey == "power"
  gfx.frame(pbx, 15, 30, 30, hot and C.fg or C.mute, 9)
  circle(pbx + 15, 31, 8, hot and accent() or C.dim, false)
  gfx.rect(pbx + 11, 21, 9, 8, C.bg)                       -- the gap in the ring
  gfx.rect(pbx + 15, 21, 1, 11, hot and accent() or C.dim)
  region(pbx, 15, 30, 30, function() power_dlg = true end, "power")
  local cl = clock_str()
  local cx = pbx - 16 - tw(cl)
  text(cx, 21, cl, C.fg)
  local ex = cx - 16 - 48
  gfx.frame(ex, 17, 48, 26, hoverkey == "exit" and C.fg or C.mute, 8)
  text(ex + 8, 21, "exit", hoverkey == "exit" and C.fg or C.dim)
  region(ex, 17, 48, 26, function() running = false end, "exit")
end

local function draw_hero()
  local x, y, w = 236, 66, W - 236 - 24
  gfx.frame(x, y, w, 92, C.line, 12)
  local t = (now() - started) // 70
  local a = typed_a:sub(1, math.min(#typed_a, t))
  local b = t > #typed_a + 6 and typed_b:sub(1, t - #typed_a - 6) or ""
  text(x + 24, y + 16, a, C.fg, 2)
  text(x + 24, y + 56, b, C.dim)
  local done = #b >= #typed_b
  if not done or (now() // 500) % 2 == 0 then
    local cxp = done and (x + 24 + tw(b)) or (#b > 0 and x + 24 + tw(b) or x + 24 + tw(a, 2))
    local cyp = (#b > 0 or done) and y + 56 or y + 16
    gfx.rect(cxp, cyp, 8, 16, accent())
  end
end

local function draw_main_icons()
  local rows = math.max(1, (H - 170) // 100)
  for i, name in ipairs(MAIN_APPS) do
    local col, row = (i - 1) // rows, (i - 1) % rows
    local x, y = 24 + col * 104, 70 + row * 100
    local key = "icon" .. name
    local hot = hoverkey == key
    if hot then gfx.round(x - 6, y - 6, 92, 92, 12, C.faint) end
    gfx.frame(x + 4, y, 64, 64, hot and C.fg or C.line, 12)
    ICONS[name](x + 8, y + 4, hot and accent() or C.fg)
    text(x + 36 - tw(name) // 2, y + 70, name, hot and C.fg or C.dim)
    region(x - 6, y - 6, 92, 92, function() open_app(name) end, key)
  end
end

-- the grid of script and app icons
local GX, GY, CW, CH = 236, 196, 96, 92
local page = 1

local function grid_dims()
  return math.max(1, (W - GX - 16) // CW), math.max(1, (H - GY - 76) // CH)
end

local function draw_launchers()
  local cols, rows = grid_dims()
  local per = cols * rows
  local pages = math.max(1, (#launchers + per - 1) // per)
  page = math.max(1, math.min(page, pages))
  local hovered = hoverkey and hoverkey:match("^l(%d+)$")
  local l = hovered and launchers[tonumber(hovered)]
  local info = l and (l.title .. ": " .. l.about) or (#launchers .. " scripts and apps. Click one to run it")
  text(GX, GY - 28, info:sub(1, (W - GX - 260) // 8), l and C.fg or C.dim)
  -- refresh and page buttons
  local rx = W - 24 - 80
  gfx.frame(rx, GY - 32, 80, 24, hoverkey == "refresh" and C.fg or C.line, 8)
  text(rx + 8, GY - 28, "refresh", hoverkey == "refresh" and C.fg or C.dim)
  region(rx, GY - 32, 80, 24, function() scan_launchers(); toast(#launchers .. " scripts and apps found") end, "refresh")
  if pages > 1 then
    local px = rx - 120
    text(px + 28, GY - 28, page .. "/" .. pages, C.dim)
    for k, d in ipairs({ -1, 1 }) do
      local bx = px + (k == 1 and 0 or 84)
      gfx.frame(bx, GY - 32, 24, 24, hoverkey == "pg" .. d and C.fg or C.line, 8)
      text(bx + 8, GY - 28, d < 0 and "<" or ">", C.fg)
      region(bx, GY - 32, 24, 24, function() page = math.max(1, math.min(pages, page + d)) end, "pg" .. d)
    end
  end
  local first = (page - 1) * per
  for slot = 0, per - 1 do
    local i = first + slot + 1
    local item = launchers[i]
    if not item then break end
    local x, y = GX + (slot % cols) * CW, GY + (slot // cols) * CH
    local key = "l" .. i
    local hot = hoverkey == key
    if hot then gfx.round(x - 4, y - 4, CW - 8, CH - 8, 12, C.faint) end
    local border = hot and C.fg or (item.kind == "app" and accent() or C.line)
    gfx.frame(x + 4, y, 64, 56, border, 12)
    local init = item.title:sub(1, 2)
    text(x + 36 - tw(init, 2) // 2, y + 6, init, hot and accent() or C.fg, 2)
    if item.kind == "script" then
      text(x + 24, y + 38, "lua", C.mute)
      circle(x + 58, y + 10, 3, accent(), true)
    else
      gfx.frame(x + 22, y + 40, 28, 10, C.mute, 2)
      gfx.rect(x + 22, y + 44, 28, 1, C.mute)
    end
    local label = item.title:sub(1, 11)
    text(x + 36 - tw(label) // 2, y + 62, label, hot and C.fg or C.dim)
    region(x - 4, y - 4, CW - 8, CH - 8, function() launch(item) end, key)
  end
end

local function draw_bottom()
  local by = H - 62
  gfx.frame(12, by, W - 24, 50, C.line, 12)
  if cfg.cat then
    local f = (now() // 350) % 4
    gfx.image(imgs["cat" .. f], 22, by + 4, 48, 44)
    region(20, by + 3, 52, 44, function() toast("meow") end, "cat")
  end
  local x = 90
  local focused = top_win()
  local maxx = W - 28 - 3 * 44 - 12
  for i, win in ipairs(wins) do
    local label = win.title:sub(1, 12)
    local wd = tw(label) + 24
    if x + wd > maxx then break end
    local active = win == focused
    gfx.frame(x, by + 9, wd, 32, active and C.fg or C.mute, 10)
    text(x + 12, by + 17, label, active and C.fg or (win.min and C.mute or C.dim))
    region(x, by + 9, wd, 32, function()
      if win.min then focus(win) elseif active then win.min = true else focus(win) end
    end, "tab" .. i)
    x = x + wd + 8
  end
  local sx = W - 28
  for i = #SOCIALS, 1, -1 do
    local s = SOCIALS[i]
    sx = sx - 36
    gfx.frame(sx, by + 7, 36, 36, hoverkey == "s" .. s[1] and C.fg or C.line, 9)
    text(sx + 10, by + 17, s[1], hoverkey == "s" .. s[1] and accent() or C.fg)
    region(sx, by + 7, 36, 36, function() toast(s[3]) end, "s" .. s[1])
    sx = sx - 8
  end
end

local function title_button(x, y, key, hot, draw)
  gfx.frame(x, y, 22, 22, hot and C.fg or C.mute, 6)
  draw(x, y, hot and accent() or C.dim)
end

local function draw_window(win, focused)
  local x, y, w, h = win.x, win.y, win.w, win.h
  gfx.round(x, y, w, h, 12, C.bg)
  gfx.frame(x, y, w, h, focused and C.fg or C.mute, 12)
  gfx.rect(x + 1, y + TITLE_H, w - 2, 1, focused and C.line or C.faint)
  text(x + 16, y + 9, ("~/" .. win.title):sub(1, (w - 130) // 8), focused and C.fg or C.dim)
  regions[#regions + 1] = { x = x, y = y, w = w, h = h, key = "win", win = win }
  local id = tostring(win)
  local bx = x + w - 32
  title_button(bx, y + 6, "x", hoverkey == "close" .. id, function(px, py, c) text(px + 7, py + 3, "x", c) end)
  region(bx, y + 6, 22, 22, function() close_win(win) end, "close" .. id)
  bx = bx - 28
  title_button(bx, y + 6, "max", hoverkey == "max" .. id, function(px, py, c) gfx.frame(px + 6, py + 6, 10, 10, c, 0) end)
  region(bx, y + 6, 22, 22, function() maximize(win) end, "max" .. id)
  bx = bx - 28
  title_button(bx, y + 6, "min", hoverkey == "min" .. id, function(px, py, c) gfx.rect(px + 6, py + 14, 10, 2, c) end)
  region(bx, y + 6, 22, 22, function() win.min = true end, "min" .. id)
  local nchrome = #regions
  -- content, clipped to the window
  local ix, iy, iw, ih = x + 1, y + TITLE_H + 1, w - 2, h - TITLE_H - 3
  gfx.clip(ix, iy, iw, ih)
  win.def.draw(win, ix, iy, iw, ih)
  gfx.clip()
  win.cx0, win.cy0 = ix, iy
  -- scrollbar for pages that overflow
  local ch = win.content_h
  if ch > ih then
    local th = math.max(24, ih * ih // ch)
    local ty = iy + (ih - th) * win.scroll // math.max(1, ch - ih)
    gfx.round(x + w - 8, ty, 4, th, 2, C.mute)
  end
  -- anything the content registered must stay inside the content area
  for i = nchrome + 1, #regions do
    local r = regions[i]
    if r.y + r.h > iy + ih then r.h = math.max(0, iy + ih - r.y) end
    if r.y < iy then r.h = r.h - (iy - r.y); r.y = iy end
  end
  -- resize grip
  gfx.line(x + w - 6, y + h - 14, x + w - 14, y + h - 6, focused and C.dim or C.faint)
  gfx.line(x + w - 6, y + h - 9, x + w - 9, y + h - 6, focused and C.dim or C.faint)
  regions[#regions + 1] = { x = x + w - 18, y = y + h - 18, w = 16, h = 16, key = "grip", win = win }
end

-- ---- power: a confirmation, then every program is stopped, the screen says goodbye and the machine powers off

local function power_action(kind)
  for i = #wins, 1, -1 do close_win(wins[i]) end                  -- stop everything that is still running
  save_cfg()
  gfx.rect(0, 0, W, H, C.bg)
  local msg = kind == "off" and "Shutting down..." or "Restarting..."
  local sub = kind == "off" and "It is safe to turn the power off when the screen goes dark." or "See you in a moment."
  gfx.frame(W // 2 - 280, H // 2 - 50, 560, 100, C.line, 14)
  text(W // 2 - tw(msg, 2) // 2, H // 2 - 34, msg, C.fg, 2)
  text(W // 2 - tw(sub) // 2, H // 2 + 14, sub, C.dim)
  gfx.present()
  leos.sleep(900)
  if kind == "off" then leos.poweroff() else leos.reboot() end
  toast("the machine did not respond to the power request")        -- only reached if the firmware refused
end

local function draw_power()
  if not power_dlg then return end
  local w, h = 460, 190
  local x, y = (W - w) // 2, (H - h) // 2
  gfx.round(x - 8, y - 8, w + 16, h + 16, 16, C.bg)               -- hide what is behind
  gfx.frame(x, y, w, h, C.fg, 14)
  text(x + 24, y + 20, "Power", C.fg, 2)
  text(x + 24, y + 64, "Stop everything and switch the machine off?", C.dim)
  local items = { { "shut down", "off", "s" }, { "restart", "restart", "r" }, { "cancel", nil, "esc" } }
  local bx = x + 24
  for i, it in ipairs(items) do
    local bw = 128
    local key = "pw" .. i
    local hot = hoverkey == key
    gfx.frame(bx, y + 110, bw, 44, hot and C.fg or (i == 1 and accent() or C.line), 12)
    if hot then gfx.round(bx + 2, y + 112, bw - 4, 40, 10, C.faint) end
    text(bx + bw // 2 - tw(it[1]) // 2, y + 110 + 8, it[1], hot and C.fg or C.fg)
    text(bx + bw // 2 - tw("(" .. it[3] .. ")") // 2, y + 110 + 26, "(" .. it[3] .. ")", C.mute)
    region(bx, y + 110, bw, 44, function()
      if it[2] then power_action(it[2]) else power_dlg = false end
    end, key)
    bx = bx + bw + 16
  end
end

local function draw_toast()
  if not toast_msg or now() > toast_until then toast_msg = nil return end
  local w = tw(toast_msg) + 40
  local x, y = (W - w) // 2, H - 130
  gfx.round(x, y, w, 40, 12, C.bg)
  gfx.frame(x, y, w, 40, accent(), 12)
  text(x + 20, y + 12, toast_msg, C.fg)
end

local function redraw()
  regions = {}
  draw_wall()
  draw_main_icons()
  draw_hero()
  draw_launchers()
  draw_taskbar()
  draw_bottom()
  local focused = top_win()
  for _, win in ipairs(wins) do if not win.min then draw_window(win, win == focused) end end
  draw_power()
  draw_toast()
end

-- ---------------------------------------------------------------- input

local function find_region(px, py)
  for i = #regions, 1, -1 do
    local r = regions[i]
    if px >= r.x and px < r.x + r.w and py >= r.y and py < r.y + r.h then return r end
  end
end

local function on_mouse(ev)
  local was_down = down
  mx, my = ev.x, ev.y
  down = (ev.buttons & 1) ~= 0
  if power_dlg then                                   -- the power dialog is modal: only its buttons react
    local r = find_region(mx, my)
    local k = r and r.key:match("^pw%d$") and r.key or nil
    if down and not was_down then
      if r and k and r.fn then r.fn() elseif not r or not k then
        local w, h = 460, 190
        if mx < (W - w) // 2 or mx > (W + w) // 2 or my < (H - h) // 2 or my > (H + h) // 2 then power_dlg = false end   -- click outside cancels
      end
      return true
    end
    if k ~= hoverkey then hoverkey = k; return true end
    return false
  end

  if ev.wheel ~= 0 then
    local win
    for i = #wins, 1, -1 do
      local w = wins[i]
      if not w.min and mx >= w.x and mx < w.x + w.w and my >= w.y and my < w.y + w.h then win = w break end
    end
    if win then
      if win.emu then win.vs = math.max(0, math.min(#win.emu.lines - 1, (win.vs or 0) + ev.wheel * 3))
      elseif win.def.wheel then win.def.wheel(win, ev.wheel)
      else
        local maxs = math.max(0, win.content_h - (win.h - TITLE_H - 3))
        win.scroll = math.max(0, math.min(maxs, win.scroll - ev.wheel * 48))
        if win.lines and win.cy and not win.emu then win.cy = math.max(1, math.min(#win.lines, win.cy - ev.wheel * 3)) end
      end
    elseif my > 190 and mx > GX then page = page - (ev.wheel > 0 and 1 or -1)
    end
    return true
  end

  if drag then
    if not down then drag = nil else
      local win = drag.win
      if drag.resize then
        win.w = math.max(280, math.min(W - win.x, drag.ow + mx - drag.mx))
        win.h = math.max(180, math.min(H - win.y, drag.oh + my - drag.my))
        win.restore = nil
      else
        win.x = math.max(-win.w + 80, math.min(W - 80, mx - drag.dx))
        win.y = math.max(0, math.min(H - 60, my - drag.dy))
      end
    end
    return true
  end

  local cap = capture
  if cap then
    if cap.min or not wins[1] then capture = nil
    else
      local r = cap.def.mouse(cap, mx - cap.cx0, my - cap.cy0, down, ev)
      if not down then capture = nil end
      return true
    end
  end

  if down and not was_down then
    local r = find_region(mx, my)
    if r then
      if r.key == "win" or r.key == "grip" then
        local win = r.win
        focus(win)
        if r.key == "grip" then drag = { win = win, resize = true, mx = mx, my = my, ow = win.w, oh = win.h }
        elseif my < win.y + TITLE_H then drag = { win = win, dx = mx - win.x, dy = my - win.y }
        elseif win.def.mouse then
          capture = win
          win.def.mouse(win, mx - win.cx0, my - win.cy0, true, ev)
        elseif win.def.click then win.def.click(win, mx - win.cx0, my - win.cy0) end
      else
        for _, w in ipairs(wins) do        -- a click on something inside a window raises it
          if not w.min and mx >= w.x and mx < w.x + w.w and my >= w.y and my < w.y + w.h then
            if w ~= top_win() then focus(w) end
            break
          end
        end
        if r.fn then r.fn() end
      end
    end
    return true
  end
  local r = find_region(mx, my)
  local k = r and r.key ~= "win" and r.key ~= "grip" and r.key or nil
  if k ~= hoverkey then hoverkey = k; return true end
  return false
end

local function on_key(ev)
  if power_dlg then
    if ev.down then
      if ev.code == 0x01 or ev.ch == "c" then power_dlg = false
      elseif ev.ch == "s" or ev.code == 0x1C then power_action("off")
      elseif ev.ch == "r" then power_action("restart") end
    end
    return true
  end
  if ev.down and (ev.mods & 6) == 6 and ev.ch == "q" then running = false return true end
  if ev.down and (ev.mods & 4) ~= 0 and ev.code == 0x0F then                         -- alt+tab: next window
    local list = {}
    for _, w in ipairs(wins) do list[#list + 1] = w end
    if #list > 1 then local w = table.remove(wins, 1); wins[#wins + 1] = w; w.min = false end
    return true
  end
  if ev.down and ev.code == 0x01 and (ev.mods & 4) ~= 0 and top_win() then close_win(top_win()) return true end   -- alt+esc closes
  local win = top_win()
  if not win then return false end
  if win.def.key then win.def.key(win, ev) return true end
  if ev.down then
    local maxs = math.max(0, win.content_h - (win.h - TITLE_H - 3))
    if ev.code == 0x150 or ev.code == 0x151 then win.scroll = math.min(maxs, win.scroll + (ev.code == 0x151 and 240 or 48)) return true end
    if ev.code == 0x148 or ev.code == 0x149 then win.scroll = math.max(0, win.scroll - (ev.code == 0x149 and 240 or 48)) return true end
  end
  return false
end

-- ---------------------------------------------------------------- main loop

local function main()
  scan_launchers()
  for _, a in ipairs(_G.arg or {}) do                  -- desktop open=snake   (an app, a script's file name or its title)
    local n = (a:match("^open=(.+)$") or a):lower()
    if app_defs[n] then open_app(n)
    else
      for _, l in ipairs(launchers) do
        if l.path:match("([^/]+)%.lua$"):lower() == n or l.title:lower() == n then launch(l) break end
      end
    end
  end
  local last_tick = 0
  local need = true
  while running do
    local ev = gfx.next(need and 0 or 30)
    while ev do
      if ev.type == "mouse" then
        if on_mouse(ev) then need = true end
        gfx.cursor(mx, my)
      else
        if on_key(ev) then need = true end
      end
      ev = gfx.next(0)
    end
    if pump() then need = true end
    local t = now()
    local front = top_win()
    for _, win in ipairs(wins) do
      win.focused = win == front
      if win.def.tick and not win.min and win.def.tick(win, t) then need = true end
    end
    if t - last_tick >= 250 then last_tick = t; need = true end      -- clock, cursor blink, cat, typewriter
    if need then
      redraw()
      gfx.present()
      gfx.cursor(mx, my)
      need = false
    end
  end
  for i = #wins, 1, -1 do close_win(wins[i]) end                        -- stop anything still running
end

local ok, err = pcall(main)
gfx.close()
if not ok then print("desktop crashed: " .. tostring(err)) end
