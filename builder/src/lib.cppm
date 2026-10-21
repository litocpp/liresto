module;
#include <rstd/macro.hpp>
export module liresto.build;
export import liresto;
import licrypto;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::path::PathBuf;

export namespace liresto::build
{
template<class T>
using BuildResult = Result<T, String>;
struct Entry {
    String  path;
    Vec<u8> contents;
    String  digest;
};
struct SourceOptions {
    String module;
    String name_space;
    String function;
};
struct Outputs {
    PathBuf blob;
    PathBuf source;
    PathBuf depfile;
};
struct Selection {
    Vec<String> include;
    Vec<String> exclude;
    bool        dotfiles {};
    bool        source_maps {};
};
auto matches(ref<str> pattern, ref<str> path) -> bool {
    auto          p = pattern.as_bytes(), s = path.as_bytes();
    usize         i {}, j {}, retry {};
    Option<usize> star;
    while (j < s.len()) {
        if (i < p.len() && (p[i] == u8('?') || p[i] == s[j])) {
            ++i;
            ++j;
        } else if (i < p.len() && p[i] == u8('*')) {
            star  = Some(i++);
            retry = j;
        } else if (star.is_some()) {
            i = *star + usize(1);
            j = ++retry;
        } else
            return false;
    }
    while (i < p.len() && p[i] == u8('*')) ++i;
    return i == p.len();
}
auto selected(ref<str> path, const Selection& selection) -> bool {
    if (! selection.source_maps && path.ends_with(".map"_str)) return false;
    if (! selection.dotfiles && (path.starts_with("."_str) || path.contains("/."_str)))
        return false;
    bool included = selection.include.is_empty();
    for (const auto& pattern : selection.include)
        included = included || matches(pattern.as_str(), path);
    for (const auto& pattern : selection.exclude)
        if (matches(pattern.as_str(), path)) return false;
    return included;
}
class Builder;
class PreparedResources {
    Vec<Entry>   entries_;
    Vec<PathBuf> dependencies_;
    friend class Builder;
    PreparedResources(Vec<Entry> entries, Vec<PathBuf> dependencies)
        : entries_(rstd::move(entries)), dependencies_(rstd::move(dependencies)) {}

public:
    auto entries() const -> slice<Entry> { return entries_.as_slice(); }
    auto dependencies() const -> slice<PathBuf> { return dependencies_.as_slice(); }
    auto emit(const SourceOptions& options, const Outputs& outputs) const -> BuildResult<empty>;
};
class Builder {
    Vec<Entry>   entries_;
    Vec<PathBuf> dependencies_;

public:
    auto dependency(ref<rstd::path::Path> path) -> BuildResult<empty> {
        auto full = rstd::fs::canonicalize(path);
        if (full.is_err())
            return Err(rstd::format("cannot resolve '{}': {}", path, full.unwrap_err()));
        for (const auto& current : dependencies_)
            if (current.as_path() == full->as_path()) return Ok(empty {});
        dependencies_.push(rstd::move(full).unwrap());
        return Ok(empty {});
    }
    auto add_memory(ref<str> path, Vec<u8> contents) -> BuildResult<empty> {
        if (! valid_path(path)) return Err(rstd::format("invalid resource path '{}'", path));
        auto digest = licrypto::sha256_hex(contents.as_slice());
        entries_.push(Entry { String::make(path), rstd::move(contents), rstd::move(digest) });
        return Ok(empty {});
    }
    auto add_file(ref<rstd::path::Path> file, ref<str> path) -> BuildResult<empty> {
        auto metadata = rstd::fs::symlink_metadata(file);
        if (metadata.is_err())
            return Err(rstd::format("cannot inspect '{}': {}", file, metadata.unwrap_err()));
        if (! metadata->is_file() || metadata->is_symlink())
            return Err(rstd::format("resource '{}' is not a regular file", file));
        auto bytes = rstd::fs::read(file);
        if (bytes.is_err())
            return Err(rstd::format("cannot read '{}': {}", file, bytes.unwrap_err()));
        rstd_try(dependency(file));
        return add_memory(path, rstd::move(bytes).unwrap());
    }
    auto add_directory(ref<rstd::path::Path> directory,
                       ref<str>              prefix,
                       const Selection&      selection = {}) -> BuildResult<empty> {
        if (! prefix.is_empty() && ! valid_path(prefix)) return Err("invalid directory prefix"_Str);
        auto metadata = rstd::fs::symlink_metadata(directory);
        if (metadata.is_err() || ! metadata->is_dir() || metadata->is_symlink())
            return Err(rstd::format("resource '{}' is not a directory", directory));
        rstd_try(dependency(directory));
        auto opened = rstd::fs::read_dir(directory);
        if (opened.is_err())
            return Err(rstd::format("cannot enumerate '{}': {}", directory, opened.unwrap_err()));
        auto items = rstd::move(opened).unwrap();
        for (auto item : items) {
            if (item.is_err())
                return Err(rstd::format("cannot enumerate '{}': {}", directory, item.unwrap_err()));
            auto entry = rstd::move(item).unwrap();
            auto name  = entry.file_name();
            auto text  = name.as_os_str().to_str();
            if (text.is_none()) return Err("resource filename is not UTF-8"_Str);
            if (! selection.dotfiles && text->starts_with("."_str)) continue;
            auto path =
                prefix.is_empty() ? String::make(*text) : rstd::format("{}/{}", prefix, *text);
            auto native = entry.path();
            auto type   = entry.file_type();
            if (type.is_err()) return Err(rstd::format("cannot inspect '{}'", native.as_path()));
            if (type->is_symlink())
                return Err(
                    rstd::format("resource symlink '{}' is not supported", native.as_path()));
            if (type->is_dir())
                rstd_try(add_directory(native.as_path(), path.as_str(), selection));
            else if (type->is_file()) {
                if (selected(path.as_str(), selection))
                    rstd_try(add_file(native.as_path(), path.as_str()));
            } else
                return Err(rstd::format("unsupported resource '{}'", native.as_path()));
        }
        return Ok(empty {});
    }
    auto prepare() && -> BuildResult<PreparedResources> {
        rstd::slice_::sort_unstable_by(
            entries_.as_mut_slice().as_mut_ref(), [](const Entry& a, const Entry& b) {
                return compare_path(a.path.as_str(), b.path.as_str()) < 0;
            });
        auto seen = rstd::collections::BTreeSet<String>::make();
        for (const auto& entry : entries_) {
            for (usize i {}; i < entry.path.len(); ++i)
                if (entry.path.as_str().as_bytes()[i] == u8('/') &&
                    seen.contains(entry.path.as_str().get(usize(), i).unwrap()))
                    return Err(
                        rstd::format("resource '{}' conflicts with a parent file", entry.path));
            seen.insert(entry.path.clone());
        }
        for (usize i(1); i < entries_.len(); ++i)
            if (entries_[i - usize(1)].path == entries_[i].path)
                return Err(rstd::format("duplicate resource path '{}'", entries_[i].path));
        return Ok(PreparedResources(rstd::move(entries_), rstd::move(dependencies_)));
    }
};
} // namespace liresto::build

namespace liresto::build
{
auto identifier(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    usize i {};
    for (auto c : value.as_bytes()) {
        if (! ((c >= u8('a') && c <= u8('z')) || (c >= u8('A') && c <= u8('Z')) || c == u8('_') ||
               (i != usize() && c >= u8('0') && c <= u8('9'))))
            return false;
        ++i;
    }
    if (value.starts_with("_"_str) || value.contains("__"_str)) return false;
    constexpr auto keywords =
        " alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq import module "_str;
    if (keywords.contains(rstd::format(" {} ", value).as_str())) return false;
    return true;
}
auto qualified(ref<str> value, ref<str> separator) -> bool {
    for (;;) {
        auto split = value.split_once(separator);
        if (! identifier(split.is_some() ? split->template get<0>() : value)) return false;
        if (split.is_none()) return true;
        value = split->template get<1>();
    }
}
auto dep_path(ref<rstd::path::Path> path) -> BuildResult<String> {
    auto value = path.to_str();
    if (value.is_none()) return Err("dependency path is not UTF-8"_Str);
    Vec<u8> result;
    for (auto c : value->as_bytes()) {
        if (c == u8('\n') || c == u8('\r') || c == u8())
            return Err("dependency path contains a newline or NUL"_Str);
        if (c == u8('$'))
            result.extend_from_slice("$$"_bytes);
        else {
            if (c == u8(' ') || c == u8('#') || c == u8(':') || c == u8('\\') || c == u8('\t'))
                result.push(u8('\\'));
            result.push(u8(c));
        }
    }
    return Ok(String::from_utf8(rstd::move(result)).unwrap());
}
auto write_changed(ref<rstd::path::Path> path, slice<u8> contents) -> BuildResult<empty> {
    auto current = rstd::fs::read(path);
    if (current.is_ok() && current->as_slice() == contents) return Ok(empty {});
    auto parent = path.parent();
    if (parent.is_some()) {
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err())
            return Err(rstd::format("cannot create '{}': {}", *parent, created.unwrap_err()));
    }
    auto result = rstd::fs::write_atomic(path, contents);
    if (result.is_err())
        return Err(rstd::format("cannot write '{}': {}", path, result.unwrap_err()));
    return Ok(empty {});
}
auto PreparedResources::emit(const SourceOptions& options, const Outputs& outputs) const
    -> BuildResult<empty> {
    if (! qualified(options.module.as_str(), "."_str) ||
        ! qualified(options.name_space.as_str(), "::"_str) ||
        ! identifier(options.function.as_str()))
        return Err("invalid generated C++ module, namespace or function name"_Str);
    auto blob_path    = outputs.blob.as_path().normalize_lexically();
    auto source_path  = outputs.source.as_path().normalize_lexically();
    auto depfile_path = outputs.depfile.as_path().normalize_lexically();
    if (blob_path.is_err() || source_path.is_err() || depfile_path.is_err() ||
        outputs.blob.as_path().as_os_str().is_empty() ||
        outputs.source.as_path().as_os_str().is_empty() ||
        outputs.depfile.as_path().as_os_str().is_empty())
        return Err("invalid output path"_Str);
    if (blob_path->as_path() == source_path->as_path() ||
        blob_path->as_path() == depfile_path->as_path() ||
        source_path->as_path() == depfile_path->as_path())
        return Err("outputs must be distinct"_Str);
    if (blob_path->as_path().parent() != source_path->as_path().parent())
        return Err("blob and source must be in the same directory"_Str);
    auto filename = blob_path->as_path().file_name();
    if (filename.is_none() || filename->to_str().is_none()) return Err("invalid blob filename"_Str);
    auto blob_name = *filename->to_str();
    for (auto c : blob_name.as_bytes())
        if (c == u8('"') || c == u8('\\') || c == u8('\n') || c == u8('\r') || c == u8() ||
            c == u8('/'))
            return Err("blob must use a simple relative filename"_Str);
    Vec<u8> blob;
    String  index;
    for (const auto& entry : entries_) {
        auto path_offset = blob.len();
        blob.extend_from_slice(entry.path.as_str().as_bytes());
        auto digest_offset = blob.len();
        blob.extend_from_slice(entry.digest.as_str().as_bytes());
        auto offset = blob.len();
        blob.extend_from_slice(entry.contents.as_slice());
        index.push_str(rstd::format("    {{rstd::usize({}), rstd::usize({}), rstd::usize({}), "
                                    "rstd::usize({}), rstd::usize({}), rstd::usize({})}},\n",
                                    path_offset,
                                    entry.path.len(),
                                    offset,
                                    entry.contents.len(),
                                    digest_offset,
                                    entry.digest.len())
                           .as_str());
    }
    if (blob.is_empty()) blob.push(u8());
    if (entries_.is_empty()) index.push_str("    {},\n"_str);
    auto source = rstd::format(
        "module {};\nimport liresto;\nnamespace {} {{\nnamespace {{\n"
        "static constexpr unsigned char resource_data[] = {{\n#embed \"{}\"\n}};\n"
        "static constexpr liresto::ResourceIndex resource_index[] = {{\n{} }};\n}}\n"
        "auto {}() -> liresto::ResourceBundle {{\n"
        "    static const auto bundle = liresto::ResourceBundle::from_static<resource_data, "
        "resource_index>(rstd::usize({})).unwrap();\n"
        "    return bundle.clone();\n}}\n}}\n",
        options.module,
        options.name_space,
        blob_name,
        index,
        options.function,
        entries_.len());
    auto depfile = rstd_try(dep_path(outputs.source.as_path()));
    depfile.push_str(": "_str);
    auto names = Vec<String>::make();
    for (const auto& path : dependencies_) names.push(rstd_try(dep_path(path.as_path())));
    rstd::slice_::sort_unstable(names.as_mut_slice().as_mut_ref());
    for (const auto& name : names) {
        depfile.push_str(name.as_str());
        depfile.push_ascii(u8(' '));
    }
    depfile.push_ascii(u8('\n'));
    rstd_try(write_changed(outputs.blob.as_path(), blob.as_slice()));
    rstd_try(write_changed(outputs.depfile.as_path(), depfile.as_str().as_bytes()));
    rstd_try(write_changed(outputs.source.as_path(), source.as_str().as_bytes()));
    return Ok(empty {});
}
} // namespace liresto::build
