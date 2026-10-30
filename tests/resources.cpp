#include <rstd/test/gtest.hpp>
import liresto;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::bytes::Bytes;
static constexpr unsigned char          data[]  = { 'a', 'h', 'e', 'l', 'l', 'o', 'b' };
static constexpr liresto::ResourceIndex index[] = {
    { usize(0), usize(1), usize(1), usize(5), usize(0), usize(0) },
    { usize(6), usize(1), usize(7), usize(0), usize(0), usize(0) },
};
TEST(Resources, StaticLookupAndLifetime) {
    auto bundle   = liresto::ResourceBundle::from_static<data, index>(usize(2)).unwrap();
    auto resource = bundle.get("a"_str).unwrap().unwrap();
    EXPECT_EQ(resource.contents(), "hello"_bytes);
    EXPECT_EQ(resource.bytes().data(), reinterpret_cast<const rstd::byte*>(data) + 1);
    EXPECT_TRUE(bundle.get("missing"_str).unwrap().is_none());
    EXPECT_TRUE(bundle.get("../a"_str).is_err());
    EXPECT_TRUE(bundle.get("b"_str).unwrap()->contents().is_empty());
    bundle = {};
    EXPECT_EQ(resource.contents(), "hello"_bytes);
}
TEST(Resources, InvalidDataAndPaths) {
    EXPECT_TRUE((liresto::ResourceBundle::from_static<data, index>(usize(3)).is_err()));
    for (auto path : array<ref<str>, 9> { ""_str,
                                          "/a"_str,
                                          "a/"_str,
                                          "a//b"_str,
                                          "a/./b"_str,
                                          "a/../b"_str,
                                          "C:/a"_str,
                                          "a\\b"_str,
                                          "a\0b"_str })
        EXPECT_FALSE(liresto::valid_path(path));
    EXPECT_TRUE(liresto::valid_path("中文/a \"b#.txt"_str));
}
TEST(Resources, StaticBytesCloneAndMutation) {
    auto bytes = Bytes::from_static<data>();
    bytes.advance(usize(1));
    bytes.truncate(usize(5));
    auto clone   = bytes.clone();
    auto refused = rstd::move(bytes).try_into_mut();
    ASSERT_TRUE(refused.is_err());
    auto mutable_bytes     = rstd::move(refused).unwrap_err().into_mut();
    mutable_bytes[usize()] = u8('H');
    EXPECT_EQ(clone.as_slice(), "hello"_bytes);
    EXPECT_EQ(mutable_bytes.as_slice(), "Hello"_bytes);
    EXPECT_NE(mutable_bytes.data(), clone.data());
    auto moved = rstd::move(clone);
    EXPECT_TRUE(clone.is_empty());
    moved.advance(moved.len());
    EXPECT_TRUE(moved.is_empty());
}
TEST(Resources, PrepareCustomEntriesAndDuplicates) {
    liresto::build::Builder builder;
    Vec<u8>                 binary;
    binary.push(u8());
    binary.push(u8(255));
    ASSERT_TRUE(builder.add_memory("payload"_str, rstd::move(binary)).is_ok());
    Vec<u8> metadata;
    metadata.extend_from_slice("{\"format\":1}"_bytes);
    ASSERT_TRUE(builder.add_memory("description.json"_str, rstd::move(metadata)).is_ok());
    auto prepared = rstd::move(builder).prepare().unwrap();
    EXPECT_EQ(prepared.entries()[usize()].path, "description.json"_str);
    EXPECT_EQ(prepared.entries()[usize(1)].contents.len(), usize(2));
    EXPECT_EQ(prepared.entries()[usize(1)].digest.len(), usize(64));
    liresto::build::Builder duplicate;
    duplicate.add_memory("x"_str, {}).unwrap();
    duplicate.add_memory("x"_str, {}).unwrap();
    EXPECT_TRUE(rstd::move(duplicate).prepare().is_err());
}
TEST(Resources, Selection) {
    liresto::build::Selection selection;
    EXPECT_FALSE(liresto::build::selected(".env"_str, selection));
    EXPECT_FALSE(liresto::build::selected("a/.env"_str, selection));
    EXPECT_FALSE(liresto::build::selected("a.js.map"_str, selection));
    EXPECT_TRUE(liresto::build::selected("assets/main.js"_str, selection));
    selection.include.push("assets/*"_Str);
    selection.exclude.push("*.txt"_Str);
    EXPECT_FALSE(liresto::build::selected("index.html"_str, selection));
    EXPECT_FALSE(liresto::build::selected("assets/a.txt"_str, selection));
}

TEST(Resources, ParentFileConflict) {
    liresto::build::Builder builder;
    builder.add_memory("a/b"_str, {}).unwrap();
    builder.add_memory("a"_str, {}).unwrap();
    builder.add_memory("a-other"_str, {}).unwrap();
    EXPECT_TRUE(rstd::move(builder).prepare().is_err());
}

TEST(Resources, EmptyAndDeterministicOutput) {
    auto                          temp = rstd::fs::TempDir::make("liresto-tests"_str).unwrap();
    auto                          root = rstd::path::PathBuf::from(temp.path());
    liresto::build::Outputs       outputs { root.join("resources.bin"_str),
                                            root.join("resources.cpp"_str),
                                            root.join("resources.d"_str) };
    liresto::build::SourceOptions options { "test.resources"_Str, "test"_Str, "resources"_Str };
    auto                          empty = liresto::build::Builder().prepare().unwrap();
    ASSERT_TRUE(empty.emit(options, outputs).is_ok());
    EXPECT_EQ(rstd::fs::read(outputs.blob.as_path()).unwrap().len(), usize(1));
    liresto::build::Builder first, second;
    for (auto path : array<ref<str>, 2> { "a"_str, "b"_str }) first.add_memory(path, {}).unwrap();
    for (auto path : array<ref<str>, 2> { "b"_str, "a"_str }) second.add_memory(path, {}).unwrap();
    auto prepared = rstd::move(first).prepare().unwrap();
    ASSERT_TRUE(prepared.emit(options, outputs).is_ok());
    auto blob   = rstd::fs::read(outputs.blob.as_path()).unwrap();
    auto source = rstd::fs::read(outputs.source.as_path()).unwrap();
    ASSERT_TRUE(rstd::move(second).prepare().unwrap().emit(options, outputs).is_ok());
    EXPECT_EQ(blob.as_slice(), rstd::fs::read(outputs.blob.as_path()).unwrap().as_slice());
    EXPECT_EQ(source.as_slice(), rstd::fs::read(outputs.source.as_path()).unwrap().as_slice());
    options.function = "operator"_Str;
    EXPECT_TRUE(prepared.emit(options, outputs).is_err());
    EXPECT_EQ(source.as_slice(), rstd::fs::read(outputs.source.as_path()).unwrap().as_slice());
    options.function = "resources"_Str;
    outputs.blob     = outputs.source.clone();
    EXPECT_TRUE(prepared.emit(options, outputs).is_err());
}
