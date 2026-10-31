LeOS desktop: running and writing Lua
=====================================

Start it:   desktop                 (or:  desktop open=snake open=terminal)
Leave it:   the "off" button, or Ctrl+Alt+Q

SCRIPT ICONS
  Every *.lua file in /home/lua and /disk/lua is an icon on the desktop. Click one to run it: it opens a
  window that shows its output live. Programs that read input (io.read) get what you type in the input
  line at the bottom of the window. Stop button / Ctrl+C stops it, Ctrl+D ends its input.
  Click "refresh" after adding scripts.

  A script can describe itself in its first lines:
      --@ title: Primes                      the name under the icon (11 letters fit)
      --@ about: sieve of Eratosthenes       shown when the mouse is over the icon
      --@ args: Limit (blank = 100000)       if present, the window first asks for arguments

  Arguments arrive in the usual `arg` table:  local limit = tonumber(arg[1]) or 100000

WRITING A SCRIPT
  Open "notes", click "new", give it a name like /disk/lua/hello.lua (without a disk: /tmp/hello.lua),
  type the program and press Ctrl+R. Saving it into /home/lua or /disk/lua makes it an icon.
  In the shell, `primes.lua 5000` also works (the shell finds scripts in the current folder,
  /home/lua and /disk/lua).

WINDOW APPS
  A file in /home/desktop/apps or /disk/apps that returns a table is an app that draws inside a desktop
  window (calculator, snake, paint, clock and minesweeper are examples). The file receives the helper
  table `ui` as its argument:

      local ui = ...
      return {
        title = "demo", w = 400, h = 300,
        init  = function(win) win.n = 0 end,
        draw  = function(win, x, y, w, h)                 -- x, y: top-left of the window's content
          ui.text(x + 20, y + 20, "clicked " .. win.n .. " times", ui.C.fg)
          ui.region(x + 20, y + 60, 120, 30, function() win.n = win.n + 1 end, "btn")   -- a clickable area
          gfx.frame(x + 20, y + 60, 120, 30, ui.C.line, 8)
        end,
        key   = function(win, ev) end,                    -- ev.code, ev.ch, ev.down, ev.mods
        tick  = function(win, ms) return false end,       -- called all the time; return true to redraw
        mouse = function(win, rx, ry, down) end,          -- press/drag inside the content (paint)
        click = function(win, rx, ry) end,                -- a click in the content
      }

THE LEOS LIBRARY (usable in any script)
  leos.spawn(prog, args...)   run a program; p:read() p:write(s) p:closeinput() p:kill()
  leos.reap()                 pid, status of a finished child
  leos.dir(path) leos.stat(path) leos.exists(path) leos.mkdir(path) leos.remove(path) leos.chdir(path)
  leos.meminfo() leos.ps() leos.diskinfo() leos.netinfo() leos.uptime() leos.sleep(ms) leos.getpid()
  leos.resolve(name) leos.ping(host) leos.tcp(host, port) leos.listen(port) leos.udp()
  gfx.*                       drawing (see user/lua/gfxlib.c): open rect frame round line text image present next ...
