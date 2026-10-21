#include <rstd/macro.hpp>
import liresto.build;
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace liresto::build;
using rstd::path::PathBuf;

auto run() -> BuildResult<empty> {
    auto args = rstd::env::args();
    (void)args.next();
    SourceOptions options;
    Outputs       outputs;
    Selection     selection;
    struct Input {
        bool    directory;
        PathBuf source;
        String  path;
    };
    Vec<Input> inputs;
    auto       value = [&]() -> BuildResult<String> {
        auto next = args.next();
        if (next.is_none()) return Err("missing argument value"_Str);
        return Ok(rstd::move(next).unwrap());
    };
    while (auto arg = args.next()) {
        auto flag = arg->as_str();
        if (flag == "--help"_str) {
            rstd::io::println("liresto-pack --module NAME --namespace NAME --function NAME "
                              "--source FILE --blob FILE --depfile FILE\n  --directory DIR PREFIX "
                              "| --file FILE ALIAS (repeatable; empty PREFIX is allowed)\n  "
                              "--include GLOB --exclude GLOB --dotfiles --source-maps\nGlob '*' "
                              "matches any path bytes, including '/'; '?' matches one byte.");
            return Ok(empty {});
        }
        if (flag == "--version"_str) {
            rstd::io::println("liresto-pack 0.1.0");
            return Ok(empty {});
        }
        if (flag == "--dotfiles"_str) {
            selection.dotfiles = true;
            continue;
        }
        if (flag == "--source-maps"_str) {
            selection.source_maps = true;
            continue;
        }
        if (flag == "--directory"_str || flag == "--file"_str) {
            auto source = rstd_try(value());
            auto path   = rstd_try(value());
            inputs.push(Input {
                flag == "--directory"_str, PathBuf::from(source.as_str()), rstd::move(path) });
        } else if (flag == "--module"_str)
            options.module = rstd_try(value());
        else if (flag == "--namespace"_str)
            options.name_space = rstd_try(value());
        else if (flag == "--function"_str)
            options.function = rstd_try(value());
        else if (flag == "--source"_str)
            outputs.source = PathBuf::from(rstd_try(value()).as_str());
        else if (flag == "--blob"_str)
            outputs.blob = PathBuf::from(rstd_try(value()).as_str());
        else if (flag == "--depfile"_str)
            outputs.depfile = PathBuf::from(rstd_try(value()).as_str());
        else if (flag == "--include"_str)
            selection.include.push(rstd_try(value()));
        else if (flag == "--exclude"_str)
            selection.exclude.push(rstd_try(value()));
        else
            return Err(rstd::format("unknown argument '{}'", flag));
    }
    if (outputs.source.as_path().as_os_str().is_empty() ||
        outputs.blob.as_path().as_os_str().is_empty() ||
        outputs.depfile.as_path().as_os_str().is_empty())
        return Err("--source, --blob and --depfile are required"_Str);
    Builder builder;
    for (const auto& input : inputs) {
        if (input.directory)
            rstd_try(builder.add_directory(input.source.as_path(), input.path.as_str(), selection));
        else
            rstd_try(builder.add_file(input.source.as_path(), input.path.as_str()));
    }
    auto prepared = rstd_try(rstd::move(builder).prepare());
    return prepared.emit(options, outputs);
}
auto main() -> int {
    auto result = run();
    if (result.is_ok()) return 0;
    rstd::io::eprintln("liresto-pack: {}", result.unwrap_err());
    return 1;
}
