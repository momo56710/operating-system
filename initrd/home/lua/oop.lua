--@ title: Objects
--@ about: classes with metatables
local Account = {}
Account.__index = Account
function Account.new(owner, balance) return setmetatable({ owner = owner, balance = balance or 0, log = {} }, Account) end
function Account:deposit(n) self.balance = self.balance + n; self.log[#self.log + 1] = "+" .. n end
function Account:withdraw(n)
  if n > self.balance then error("insufficient funds", 2) end
  self.balance = self.balance - n; self.log[#self.log + 1] = "-" .. n
end
Account.__tostring = function(a) return a.owner .. ": " .. a.balance end
local Savings = setmetatable({}, { __index = Account })
Savings.__index = Savings
Savings.__tostring = Account.__tostring
function Savings.new(owner, b) local o = Account.new(owner, b); return setmetatable(o, Savings) end
function Savings:addinterest(p) self:deposit(self.balance * p // 100) end
local a = Account.new("momo", 100)
a:deposit(50); a:withdraw(30)
print(a, table.concat(a.log, " "))
print(pcall(a.withdraw, a, 1000))
local s = Savings.new("cat", 1000)
s:addinterest(5)
print(s)
