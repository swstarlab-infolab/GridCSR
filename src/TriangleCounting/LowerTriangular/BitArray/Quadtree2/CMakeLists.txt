set(outFileName TriangleCounting-LowerTriangular-BitArray-QuadTree2)
file(GLOB_RECURSE files ${CMAKE_CURRENT_SOURCE_DIR}/*)
cuda_add_executable(${outFileName} ${files})
add_dependencies(${outFileName} GridCSR BuddySystem)
target_link_libraries(${outFileName} pthread stdc++fs GridCSR BuddySystem boost_fiber boost_context gdrapi cuda)