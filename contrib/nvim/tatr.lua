-- Project-local tatr integration. Review and trust this file in Neovim.
local tatr = "tatr" -- Or an absolute path to your trusted tatr executable.

if vim.fn.executable(tatr) == 1 then
  vim.api.nvim_create_user_command("Tasks", function(opts)
    local command = vim.list_extend({ tatr, "ls" }, opts.fargs)
    local output = vim.fn.systemlist(command)

    if vim.v.shell_error ~= 0 then
      return vim.notify(table.concat(output, "\n"), vim.log.levels.ERROR)
    end

    vim.fn.setqflist({}, "r", {
      title = "Tasks",
      lines = output,
      efm = "%f:%l:%m",
    })
    vim.cmd("copen")
  end, { nargs = "*", desc = "List and filter tatr tasks" })

  vim.keymap.set("n", "<leader>tt", "<cmd>Tasks<CR>",
    { desc = "List tasks" })
  vim.keymap.set("n", "<leader>td", "<cmd>Explore<CR>",
    { desc = "Browse current file's directory" })
end
