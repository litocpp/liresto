"""Exercise resource generation and directory invalidation through the public Lito CLI."""

import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lito", required=True)
    parser.add_argument("--llvm", default="/nix/opt/llvm/22/bin")
    parser.add_argument("--build-dir")
    parser.add_argument("--isolated-host", action="store_true")
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[1]
    workspace = repo.parent
    root = pathlib.Path(tempfile.mkdtemp(prefix="liresto-integration-"))
    build = pathlib.Path(args.build_dir) if args.build_dir else root / "build"

    def write(path, content):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    quote = json.dumps
    relative = lambda path: os.path.relpath(path, root)
    write("lito.toml", f'''[package]
name = "liresto-integration"
version = "0.1.0"
[lib]
name = "liresto-integration"
module = "integration.resources"
archive = "integration.resources"
[[bin]]
name = "liresto-integration"
sources = ["main.cpp"]
link-stdlib = false
[dependencies]
liresto = {{ path = {quote(relative(repo / "core"))}, pub = true, usage = ["compile", "link", "tool"] }}
lito-resource = {{ path = {quote(relative(repo / "script"))} }}
rstd-nostd = {{ version = "0.1.9", usage = "link" }}
[build-tools.python]
path = {quote(__import__("sys").executable)}
[profile.base]
exceptions = false
rtti = false
''')
    patches = "\n".join(
        f'{name} = {{ path = {quote(str(workspace / "rstd"))} }}'
        for name in ("rstd-basic", "rstd-runtime", "rstd-core", "rstd-alloc", "rstd-parse", "rstd-std", "rstd-nostd")
    )
    target_options = '[build]\noptions = ["-DLIRESTO_TARGET_ONLY=1"]\n' if args.isolated_host else ""
    write(".lito/config.toml", f'''{target_options}[environment]
append-path = [{quote(args.llvm)}]
[patch.litocpp]
{patches}
licrypto = {{ path = {quote(str(workspace / "licrypto"))} }}
''')
    write("src/lib.cppm", "export module integration.resources;\nexport import liresto;\nexport namespace integration { auto resources() -> liresto::ResourceBundle; }\n")
    write("main.cpp", '''import integration.resources;
using namespace rstd::prelude;
using namespace rstd::literals;
auto main() -> int {
    auto bundle = integration::resources();
    for (usize i {}; i < bundle.len(); ++i) {
        auto entry = bundle.at(i).unwrap();
        rstd::io::println("{} {} {}", entry.path(), entry.len(), *entry.digest());
    }
}
''')
    write("generate.py", '''import pathlib, shutil, sys
source, target = map(pathlib.Path, sys.argv[1:])
shutil.copytree(source, target, symlinks=True)
''')
    write("build.lua", '''local resources = require("@lito.resource")
local target = lito.target({kind="lib", name="liresto-integration"})
local tree = lito.run({tool=lito.tool("python"), cwd=".",
  inputs={"generate.py", "assets"}, args={"@INPUT:1@", "@INPUT:2@", "@OUTPUT:1@"},
  outputs={{path="site", directory=true}}})
resources.embed({target=target, module="integration.resources", namespace="integration",
  name="resources", directories={{input=tree.outputs[1]}}})
''')
    write("assets/index.html", "home")
    write("assets/empty", "")
    write('assets/中文/quote " #$.txt', "text")
    write("assets/.env", "private")
    write("assets/script.js.map", "private")
    (root / "assets/empty-directory").mkdir()
    invocation = [args.lito, "build", "--offline", "--profile", "release", "-j", "4", "--build-dir", str(build)]
    calls = 0

    def run_build(success=True):
        nonlocal calls
        calls += 1
        result = subprocess.run(invocation, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / f"build-{calls}.log").write_text(result.stdout)
        assert (result.returncode == 0) == success, result.stdout[-5000:]
        return result.stdout

    def contents():
        return subprocess.check_output([build / "bin/liresto-integration/liresto-integration"], text=True)

    run_build()
    if args.isolated_host:
        assert (build / "plugin-host/bin/liresto/liresto-pack").is_file()
    else:
        assert (build / "bin/liresto/liresto-pack").is_file()
        assert not (build / "plugin-host").exists()
    before = contents()
    assert "index.html 4 " in before and "empty 0 " in before
    assert ".env" not in before and ".map" not in before
    assert '中文/quote " #$.txt 4 ' in before
    unchanged = run_build()
    assert "0 compiled" in unchanged and "[build-tool-run]" not in unchanged
    write("assets/index.html", "changed")
    run_build()
    assert "index.html 7 " in contents()
    write("assets/nested/new.js", "new")
    run_build()
    assert "nested/new.js 3 " in contents()
    (root / "assets/nested/new.js").rename(root / "assets/nested/renamed.js")
    (root / "assets/empty").unlink()
    run_build()
    assert "new.js" not in contents() and "renamed.js" in contents() and "empty 0" not in contents()
    published = build / "generated/liresto-integration/site"
    assert not (published / "nested/new.js").exists() and not (published / "empty").exists()
    (root / "assets/empty-directory").rmdir()
    run_build()
    assert not (published / "empty-directory").exists()
    script = (root / "build.lua").read_text()
    write("build.lua", script.replace('name="resources", directories=', 'name="resources", exclude={"*.js"}, directories='))
    run_build()
    assert "renamed.js" not in contents()
    write("build.lua", script)
    run_build()
    assert "renamed.js" in contents()
    (root / "assets/bad-link").symlink_to(root / "assets/index.html")
    run_build(False)
    (root / "assets/bad-link").unlink()
    run_build()
    old = contents()
    (root / "assets").rename(root / "hidden-assets")
    assert contents() == old
    print(f"directory pipeline, cache invalidation and detached execution passed; logs: {root}")


if __name__ == "__main__":
    main()
