cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)
project(aggregate C CXX)

include_directories(src lib /usr/local/include /usr/include/libxml2 /usr/lib/)

add_library(
  na STATIC
  src/news-aggregator.cc
  src/log.cc
  src/utils.cc
  src/stream-tokenizer.cc
  src/rss-feed.cc
  src/rss-feed-list.cc
  src/html-document.cc
  src/rss-index.cc)
add_executable(aggregate src/aggregate.cc)
target_link_libraries(
  aggregate
  na
  pthread
  m
  xml2
  ssl
  crypto)
