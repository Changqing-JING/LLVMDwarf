cmake_minimum_required(VERSION 3.5)
project(LLVMDwarf)
set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

aux_source_directory(src sourceFiles)

add_executable(${PROJECT_NAME} ${sourceFiles})

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs 
    support core codegen object debuginfodwarf
    AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsInfos
)
target_link_libraries(${PROJECT_NAME} ${llvm_libs})