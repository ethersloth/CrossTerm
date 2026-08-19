# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/CrossTerm_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/CrossTerm_autogen.dir/ParseCache.txt"
  "CrossTerm_autogen"
  )
endif()
