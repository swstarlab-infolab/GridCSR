set(outFileName Adj6ToGCSR-Shard)

file(GLOB_RECURSE files ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_executable(${outFileName} ${files})
add_dependencies(${outFileName} GridCSR)
target_link_libraries(${outFileName} pthread tbb stdc++fs GridCSR boost_fiber boost_context)
