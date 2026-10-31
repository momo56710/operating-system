--@ title: Brainf***
--@ about: a tiny esoteric interpreter
local function run(prog)
  local tape, ptr, pc, out = {}, 1, 1, {}
  local jump, stack = {}, {}
  for i = 1, #prog do
    local c = prog:sub(i, i)
    if c == "[" then stack[#stack + 1] = i elseif c == "]" then local j = table.remove(stack); jump[i], jump[j] = j, i end
  end
  while pc <= #prog do
    local c = prog:sub(pc, pc)
    if c == ">" then ptr = ptr + 1 elseif c == "<" then ptr = ptr - 1
    elseif c == "+" then tape[ptr] = ((tape[ptr] or 0) + 1) % 256 elseif c == "-" then tape[ptr] = ((tape[ptr] or 0) - 1) % 256
    elseif c == "." then out[#out + 1] = string.char(tape[ptr] or 0)
    elseif c == "[" and (tape[ptr] or 0) == 0 then pc = jump[pc]
    elseif c == "]" and (tape[ptr] or 0) ~= 0 then pc = jump[pc] end
    pc = pc + 1
  end
  return table.concat(out)
end
print(run("++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+."))
