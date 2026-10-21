import liresto.example;
using namespace rstd::prelude;
using namespace rstd::literals;
auto main() -> int {
    auto bundle = example::resources();
    auto file   = bundle.get("index.html"_str).unwrap();
    if (file.is_none() || file->contents() != "<html>liresto</html>\n"_bytes) return 1;
    auto descriptor = bundle.get("custom.json"_str).unwrap();
    if (descriptor.is_none() || descriptor->contents() != "{\"kind\":\"example\"}"_bytes) return 2;
    auto payload = bundle.get("payload.bin"_str).unwrap();
    if (payload.is_none() || payload->contents() != "\x00\x01\x11"_bytes) return 4;
    auto held = file->bytes();
    bundle    = {};
    file      = None();
    if (held.as_slice() != "<html>liresto</html>\n"_bytes) return 3;
    rstd::io::println("embedded resources verified");
    return 0;
}
