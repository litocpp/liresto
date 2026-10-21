local resource = require("@lito.resource")
local target = lito.target({ kind = "lib", name = "liresto-example" })
local descriptor = lito.write({ output = "custom.json", content = '{"kind":"example"}' })
local binary = lito.write({ output = "payload.bin", content = string.char(0, 1, 17) })
resource.embed({
  target = target, module = "liresto.example", namespace = "example", name = "resources",
  directories = { { input = "assets" } },
  files = {
    { input = descriptor.output, path = "custom.json" },
    { input = binary.output, path = "payload.bin" },
  },
})
