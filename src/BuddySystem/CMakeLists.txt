set(outFileName BuddySystem)

file(GLOB_RECURSE files ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_library(${outFileName} STATIC ${files})