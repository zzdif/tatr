-- Project-local tatr integration. Review and trust this file in Neovim.
local tatr = "tatr" -- Or an absolute path to your trusted tatr executable.

if vim.fn.executable(tatr) == 1 then
  local last_args = {}

  local function show_error(output)
    vim.notify(table.concat(output, "\n"), vim.log.levels.ERROR)
  end

  -- tatr prints task paths relative to the directory it runs from
  -- ("./tasks/..." from the project root, "../tasks/..." deeper down).
  -- Drop the redundant "./" prefix so every quickfix row renders alike.
  local function normalize(lines)
    return vim.tbl_map(function(line)
      return (line:gsub("^%./", "", 1))
    end, lines)
  end

  local function list_tasks(args)
    local output = vim.fn.systemlist(vim.list_extend({ tatr, "ls" }, args))
    if vim.v.shell_error ~= 0 then
      return show_error(output)
    end
    vim.fn.setqflist({}, "r", {
      title = "Tasks",
      lines = normalize(output),
      efm = "%f:%l:%m",
    })
    vim.cmd("copen")
  end

  -- Resolve the task folder to browse: the current TASK.md buffer, or — in
  -- the quickfix list — the task entry under the cursor. nil when not a task.
  local function browse_target_dir()
    local name = vim.api.nvim_buf_get_name(0)
    if name == "" and vim.bo.buftype == "quickfix" then
      local item = vim.fn.getqflist()[vim.fn.line(".")]
      if item and item.bufnr > 0 then
        name = vim.fn.bufname(item.bufnr)
      end
    end
    if name == "" or vim.fn.fnamemodify(name, ":t") ~= "TASK.md" then
      return nil
    end
    return vim.fn.fnamemodify(name, ":p:h")
  end

  -- Browse a task folder in a new window above the current one.
  local function explore_task_dir()
    local dir = browse_target_dir()
    if not dir then
      return vim.notify("Not in a task: open a TASK.md or select one in the task list first", vim.log.levels.ERROR)
    end
    vim.cmd("aboveleft split")
    if vim.fn.exists(":Explore") > 0 then
      return vim.cmd("Explore " .. vim.fn.fnameescape(dir))
    end
    if pcall(require, "oil") then
      return require("oil").open(dir)
    end
    vim.notify("No file browser: enable netrw or install oil.nvim", vim.log.levels.ERROR)
  end

  vim.api.nvim_create_user_command("Tasks", function(opts)
    local fargs = opts.fargs
    if fargs[1] == "explore" then
      return explore_task_dir()
    end
    if fargs[1] == "new" then
      local rest = {}
      for i = 2, #fargs do
        rest[#rest + 1] = fargs[i]
      end
      local output = vim.fn.systemlist(vim.list_extend({ tatr, "new" }, rest))
      if vim.v.shell_error ~= 0 then
        return show_error(output)
      end
      vim.notify(table.concat(output, "\n"), vim.log.levels.INFO)
      -- Refresh the list using the filters of the previous :Tasks call.
      return list_tasks(last_args)
    end
    last_args = fargs
    list_tasks(fargs)
  end, { nargs = "*", desc = "tatr ls filters, :Tasks new Title, or :Tasks explore" })

  vim.keymap.set("n", "<leader>tt", "<cmd>Tasks<CR>",
    { desc = "List tasks" })
  vim.keymap.set("n", "<leader>td", "<cmd>Tasks explore<CR>",
    { desc = "Browse current file's directory" })
end
