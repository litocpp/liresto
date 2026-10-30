local lito = require("@lito")

local function embed(options)
  if type(options) ~= "table" then error("resource.embed: expected options table") end
  local allowed = { target = true, tool = true, module = true, namespace = true, name = true,
    output = true, directories = true, files = true, include = true, exclude = true,
    dotfiles = true, source_maps = true, roots = true }
  for key in pairs(options) do
    if not allowed[key] then error("resource.embed: unknown option " .. tostring(key)) end
  end
  if not options.target then error("resource.embed: target is required") end
  for _, key in ipairs({ "dotfiles", "source_maps" }) do
    if options[key] ~= nil and type(options[key]) ~= "boolean" then
      error("resource.embed: " .. key .. " must be a boolean")
    end
  end
  local args, inputs, input_roots = {}, {}, {}
  local function argument(flag, value)
    if type(value) ~= "string" then error("resource.embed: " .. flag .. " must be a string") end
    args[#args + 1] = flag
    args[#args + 1] = value
  end
  argument("--module", options.module)
  argument("--namespace", options.namespace)
  argument("--function", options.name)
  argument("--source", "@OUTPUT:1@")
  argument("--blob", "@OUTPUT:2@")
  argument("--depfile", "@OUTPUT:3@")
  for _, kind in ipairs({ "directories", "files" }) do
    for _, entry in ipairs(options[kind] or {}) do
      if type(entry) ~= "table" then error("resource.embed: expected " .. kind .. " entry table") end
      for key in pairs(entry) do
        if key ~= "input" and key ~= "path" and not (key == "root" and kind == "directories") then
          error("resource.embed: unknown " .. kind .. " entry option " .. tostring(key))
        end
      end
      if (entry.input == nil) == (entry.root == nil) then
        error("resource.embed: entry requires exactly one input or external source root")
      end
      local path = entry.path or (kind == "directories" and "" or nil)
      if type(path) ~= "string" then error("resource.embed: file path must be a string") end
      if kind == "directories" then
        if entry.root then
          input_roots[#input_roots + 1] = entry.root
          argument("--directory", "@INPUT_ROOT:" .. #input_roots .. "@")
        else
          inputs[#inputs + 1] = entry.input
          argument("--directory", "@INPUT:" .. #inputs .. "@")
        end
      else
        inputs[#inputs + 1] = entry.input
        argument("--file", "@INPUT:" .. #inputs .. "@")
      end
      args[#args + 1] = path
    end
  end
  for _, kind in ipairs({ "include", "exclude" }) do
    for _, pattern in ipairs(options[kind] or {}) do argument("--" .. kind, pattern) end
  end
  if options.dotfiles then args[#args + 1] = "--dotfiles" end
  if options.source_maps then args[#args + 1] = "--source-maps" end
  local output = options.output or "resources/bundle"
  local depfile = { output = 3 }
  if options.roots and #options.roots > 0 then depfile.roots = options.roots end
  local result = lito.run({
    tool = options.tool or lito.host_tool(options.target, "liresto", "liresto-pack"),
    cwd = ".", args = args, inputs = #inputs > 0 and inputs or nil,
    input_roots = #input_roots > 0 and input_roots or nil,
    outputs = { output .. ".cpp", output .. ".bin", output .. ".d" },
    depfile = depfile,
  })
  lito.target_add_generated_source(options.target, result.outputs[1])
  return { source = result.outputs[1], data = result.outputs[2], depfile = result.outputs[3] }
end

return { embed = embed }
