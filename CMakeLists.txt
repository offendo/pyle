cmake_minimum_required(VERSION 3.15)
project(pyle LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# —————————————
# 1) find pybind11
# —————————————
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

# —————————————
# 2) locate Lean sysroot
# —————————————
execute_process(
  COMMAND lean --print-prefix
  OUTPUT_VARIABLE LEAN_PREFIX
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(LEAN_INCLUDE_DIR "${LEAN_PREFIX}/include")
set(LEAN_LIB_DIR     "${LEAN_PREFIX}/lib/lean")

# ————————————————————
# 3) run lake build now
# ————————————————————
execute_process(
  COMMAND lake --dir=${CMAKE_SOURCE_DIR} build
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  RESULT_VARIABLE _lake_build_res
)
if (NOT _lake_build_res EQUAL 0)
    message(FATAL_ERROR "Failed to run `lake build`")
endif()

# where lake deposits its libs
set(LAKE_LIB_DIR "${CMAKE_SOURCE_DIR}/.lake/build/lib")

# ——————————————————————————
# 4) import the Pyle shared library
# ——————————————————————————
find_library(PYLELIB
  NAMES Pyle
  PATHS ${LAKE_LIB_DIR}
  NO_DEFAULT_PATH
)
if (NOT PYLELIB)
    message(FATAL_ERROR "Could not find libPyle in ${LAKE_LIB_DIR}")
endif()

add_library(Pyle SHARED IMPORTED)
set_target_properties(Pyle PROPERTIES
  IMPORTED_LOCATION "${PYLELIB}"
)

# ——————————————————————————————
# 5) import Lean’s runtime libraries
# ——————————————————————————————
foreach(libname IN ITEMS Init_shared leanshared_1 leanshared)
    find_library(${libname}_LIB
      NAMES ${libname}
      PATHS ${LEAN_LIB_DIR}
      NO_DEFAULT_PATH
    )
    if (NOT ${libname}_LIB)
        message(FATAL_ERROR "Could not find lib${libname} in ${LEAN_LIB_DIR}")
    endif()
    add_library(${libname} SHARED IMPORTED)
    set_target_properties(${libname} PROPERTIES
      IMPORTED_LOCATION "${${libname}_LIB}"
    )
endforeach()

# —————————————————————
# 6) the Python extension
# —————————————————————
pybind11_add_module(pyle
  csrc/pyle.cpp
)

target_include_directories(pyle PRIVATE
  ${LEAN_INCLUDE_DIR}
)

target_link_libraries(pyle PRIVATE
  Pyle
  Init_shared
  leanshared_1
  leanshared
)

# ——————————————————————
# 7) RPATH so runtime can find the .so files
# ——————————————————————
if(APPLE)
    set(rpt "@loader_path")
else()
    set(rpt "$ORIGIN")
endif()

set_target_properties(pyle PROPERTIES
  CXX_STANDARD            17
  BUILD_WITH_INSTALL_RPATH ON
  BUILD_RPATH             "${rpt};${LEAN_LIB_DIR};${LAKE_LIB_DIR}"
  INSTALL_RPATH           "${rpt};${LEAN_LIB_DIR};${LAKE_LIB_DIR}"
)

# ——————————————————————
# 8) Explicitly install
# ——————————————————————
install(TARGETS pyle DESTINATION .)
install(FILES ${PYLELIB} DESTINATION .)
