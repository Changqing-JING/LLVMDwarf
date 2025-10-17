cmake_minimum_required(VERSION 3.5)
project(LLVMDwarf)
set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Original high-level DIBuilder example (for comparison - too much overhead)
add_executable(${PROJECT_NAME} src/main.cpp)

# Simple DIE-based DWARF generator (recommended middle-layer solution)
# - Uses DIE classes for automatic type reference management
# - Direct human-readable output, no binary serialization complexity
add_executable(${PROJECT_NAME}_Simple src/main_simple.cpp)

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs 
    support core codegen object debuginfodwarf mc
    AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsInfos
)
target_link_libraries(${PROJECT_NAME} ${llvm_libs})
target_link_libraries(${PROJECT_NAME}_Simple ${llvm_libs})