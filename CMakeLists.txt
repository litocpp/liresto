cmake_minimum_required(VERSION 3.29)

project(
  liresto
  VERSION 0.1.0
  LANGUAGES CXX)

add_library(liresto STATIC)
target_compile_features(liresto PUBLIC cxx_std_20)
target_sources(
  liresto
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         core/src/lib.cppm
         core/src/resource.cppm
         core/src/build.cppm)
target_link_libraries(
  liresto
  PUBLIC rstd.std
  PRIVATE licrypto)

add_executable(liresto-pack EXCLUDE_FROM_ALL core/bin/pack.cpp)
target_link_libraries(liresto-pack PRIVATE liresto rstd::nostd)
