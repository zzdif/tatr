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
  vim.cmd("Tasks explore")
  assert(vim.fn.resolve(vim.b.netrw_curdir) == vim.fn.resolve(task_dir))
  assert(vim.fn.maparg("\\td", "n"):lower() == "<cmd>tasks explore<cr>")

  -- From the quickfix window, explore follows the entry under the cursor,
  -- opening above the task list even when 'splitbelow' is set.
  vim.o.splitbelow = true
  vim.cmd("Tasks")
  local qf_win = vim.api.nvim_get_current_win()
  local windows = #vim.api.nvim_list_wins()
  local item = vim.fn.getqflist()[vim.fn.line(".")] -- Entry under the cursor.
  local entry_dir = vim.fn.fnamemodify(vim.fn.bufname(item.bufnr), ":p:h")
  vim.cmd("Tasks explore")
  assert(#vim.api.nvim_list_wins() == windows + 1) -- Opens in a new split.
  assert(vim.fn.win_screenpos(vim.api.nvim_get_current_win())[1]
    < vim.fn.win_screenpos(qf_win)[1]) -- Above the task list.
  assert(vim.fn.resolve(vim.b.netrw_curdir) == vim.fn.resolve(entry_dir))

  -- Exploring without a task context must fail without opening a window:
  -- unnamed buffers and named non-task files are both rejected.
  vim.cmd("enew")
  local no_task = false
  vim.notify = function() no_task = true end
  vim.cmd("Tasks explore")
  vim.cmd("edit .gitignore")
  local not_task_file = false
  vim.notify = function() not_task_file = true end
  vim.cmd("Tasks explore")
  assert(no_task and not_task_file and #vim.api.nvim_list_wins() == windows + 1)
  assert(vim.o.makeprg == makeprg and vim.o.errorformat == errorformat)

  vim.cmd("Tasks :absent")
  assert(vim.fn.getqflist()[1].valid == 0)
  local notified = false
  vim.notify = function() notified = true end
  vim.cmd("Tasks invalid_query")
  assert(notified)
  assert(vim.fn.getqflist()[1].text:find("No tasks were found"))

  -- Report rows must render uniformly regardless of the working directory.
  vim.cmd("Tasks")
  assert(#vim.fn.getqflist() == 1)
  vim.cmd("Tasks new -t lab -s lab Fixture created task") -- Distinct HUID suffix avoids same-second collisions.
  local entries = vim.fn.getqflist()
  assert(#entries == 2) -- Creation refreshes the previous plain listing.
  local created = false
  for _, item in ipairs(entries) do
    assert(not vim.fn.bufname(item.bufnr):match("^%.%/"))
    if item.text:find("Fixture created task") then created = true end
  end
  assert(created and vim.fn.filereadable(vim.fn.bufname(entries[1].bufnr)) == 1)
  local notify_text
  vim.notify = function(message) notify_text = message end
  vim.cmd("Tasks new -s second Second created task")
  assert(notify_text and notify_text:find("Second created task"))
  assert(#vim.fn.getqflist() == 3) -- last_args was the unfiltered listing.
  local failed = false
  vim.notify = function() failed = true end
  vim.cmd("Tasks new -p notanumber Broken task") -- Deterministic tatr failure.
  assert(failed)
  assert(#vim.fn.getqflist() == 3) -- A failed creation must not refresh.
end
vim.cmd("qa!")
