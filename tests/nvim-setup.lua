-- Run by tests/setup.sh in an isolated project and Neovim environment.
if vim.env.TATR_TEST_MISSING == "1" then
  vim.env.PATH = ""
  dofile(".nvim.lua")
  assert(vim.fn.exists(":Tasks") == 0)
  assert(vim.fn.maparg("\\tt", "n") == "")
  assert(vim.fn.maparg("\\td", "n") == "")
else
  local makeprg, errorformat = vim.o.makeprg, vim.o.errorformat
  dofile(".nvim.lua")
  assert(vim.fn.exists(":Tasks") == 2)
  assert(vim.fn.maparg("\\tt", "n") ~= "")
  assert(vim.fn.maparg("\\td", "n") ~= "")

  vim.cmd("Tasks :bug")
  local entries = vim.fn.getqflist()
  assert(#entries == 1 and entries[1].valid == 1)
  vim.cmd("cfirst")
  assert(vim.fn.expand("%:t") == "TASK.md")
  local task_dir = vim.fn.expand("%:p:h")
  vim.cmd("Explore")
  assert(vim.fn.resolve(vim.b.netrw_curdir) == vim.fn.resolve(task_dir))
  assert(vim.o.makeprg == makeprg and vim.o.errorformat == errorformat)

  vim.cmd("Tasks :absent")
  assert(vim.fn.getqflist()[1].valid == 0)
  local notified = false
  vim.notify = function() notified = true end
  vim.cmd("Tasks invalid_query")
  assert(notified)
  assert(vim.fn.getqflist()[1].text:find("No tasks were found"))
end
vim.cmd("qa!")
