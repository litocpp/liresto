export module liresto;
export import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace liresto
{

enum class Error
{
    InvalidPath,
    InvalidData
};

auto compare_path(ref<str> a, ref<str> b) -> int {
    auto left = a.as_bytes(), right = b.as_bytes();
    for (usize i {}; i < left.len() && i < right.len(); ++i) {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
    }
    return left.len() < right.len() ? -1 : left.len() > right.len() ? 1 : 0;
}

auto valid_path(ref<str> path) -> bool {
    if (path.is_empty()) return false;
    for (auto value : path.as_bytes())
        if (value == u8() || value == u8('\\') || value == u8(':')) return false;
    for (;;) {
        auto split   = path.split_once("/"_str);
        auto segment = split.is_some() ? split->template get<0>() : path;
        if (segment.is_empty() || segment == "."_str || segment == ".."_str) return false;
        if (split.is_none()) return true;
        path = split->template get<1>();
    }
}

struct ResourceIndex {
    usize path_offset;
    usize path_length;
    usize offset;
    usize length;
    usize digest_offset;
    usize digest_length;
};

class Resource {
    ref<str>           path_;
    ref<str>           digest_;
    rstd::bytes::Bytes bytes_;
    friend class ResourceBundle;
    Resource(ref<str> path, ref<str> digest, rstd::bytes::Bytes bytes)
        : path_(path), digest_(digest), bytes_(rstd::move(bytes)) {}

public:
    auto path() const -> ref<str> { return path_; }
    auto digest() const -> Option<ref<str>> {
        if (digest_.is_empty()) return None();
        return Some(ref<str>(digest_));
    }
    auto contents() const -> slice<u8> { return bytes_.as_slice(); }
    auto bytes() const -> rstd::bytes::Bytes { return bytes_.clone(); }
    auto len() const -> usize { return bytes_.len(); }
};

class ResourceBundle {
    rstd::bytes::Bytes   data_;
    slice<ResourceIndex> entries_;
    ResourceBundle(rstd::bytes::Bytes data, slice<ResourceIndex> entries)
        : data_(rstd::move(data)), entries_(entries) {}
    auto text(usize offset, usize length) const -> ref<str> {
        return rstd::str_::from_utf8(
                   slice<u8>::from_raw_parts(data_.data() + offset.to_primitive(), length))
            .unwrap();
    }

public:
    ResourceBundle() = default;
    // Both arrays must be immutable static storage, including the offset-only index.
    template<auto& Data, auto& Entries>
        requires(__is_array(rstd::mtp::rm_ref<decltype(Entries)>) &&
                 rstd::mtp::is_const<rstd::mtp::rm_ref<decltype(Entries[0])>>)
    static auto from_static(usize count) -> Result<ResourceBundle, Error> {
        if (count > usize(sizeof(Entries) / sizeof(Entries[0]))) return Err(Error::InvalidData);
        auto     data    = rstd::bytes::Bytes::from_static<Data>();
        auto     entries = slice<ResourceIndex>::from_raw_parts(Entries, count);
        ref<str> previous;
        for (const auto& entry : entries) {
            auto bounded = [&](usize offset, usize length) {
                return offset <= data.len() && length <= data.len() - offset;
            };
            if (! bounded(entry.offset, entry.length) ||
                ! bounded(entry.path_offset, entry.path_length) ||
                ! bounded(entry.digest_offset, entry.digest_length))
                return Err(Error::InvalidData);
            auto path = rstd::str_::from_utf8(slice<u8>::from_raw_parts(
                data.data() + entry.path_offset.to_primitive(), entry.path_length));
            if (path.is_err() || ! valid_path(*path) ||
                (! previous.is_empty() && compare_path(previous, *path) >= 0))
                return Err(Error::InvalidData);
            previous = *path;
            if (entry.digest_length != usize() && entry.digest_length != usize(64))
                return Err(Error::InvalidData);
            for (usize i {}; i < entry.digest_length; ++i) {
                auto c = data[entry.digest_offset + i];
                if (! ((c >= u8('0') && c <= u8('9')) || (c >= u8('a') && c <= u8('f'))))
                    return Err(Error::InvalidData);
            }
        }
        return Ok(ResourceBundle(rstd::move(data), entries));
    }
    auto clone() const -> ResourceBundle { return ResourceBundle(data_.clone(), entries_); }
    auto len() const -> usize { return entries_.len(); }
    auto at(usize index) const -> Option<Resource> {
        if (index >= len()) return None();
        const auto& entry = entries_[index];
        auto        bytes = data_.clone();
        bytes.advance(entry.offset);
        bytes.truncate(entry.length);
        return Some(Resource(text(entry.path_offset, entry.path_length),
                             text(entry.digest_offset, entry.digest_length),
                             rstd::move(bytes)));
    }
    auto get(ref<str> path) const -> Result<Option<Resource>, Error> {
        if (! valid_path(path)) return Err(Error::InvalidPath);
        usize first {}, last = len();
        while (first < last) {
            auto        mid   = first + (last - first) / usize(2);
            const auto& entry = entries_[mid];
            auto        name  = text(entry.path_offset, entry.path_length);
            if (compare_path(name, path) < 0)
                first = mid + usize(1);
            else if (compare_path(name, path) > 0)
                last = mid;
            else
                return Ok(at(mid));
        }
        return Ok(None<Resource>());
    }
    auto is_directory(ref<str> path) const -> bool {
        if (path.is_empty()) return true;
        if (! valid_path(path)) return false;
        auto prefix = rstd::format("{}/", path);
        for (const auto& entry : entries_)
            if (text(entry.path_offset, entry.path_length).starts_with(prefix.as_str()))
                return true;
        return false;
    }
};
} // namespace liresto
