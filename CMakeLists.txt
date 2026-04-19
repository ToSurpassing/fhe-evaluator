cmake_minimum_required(VERSION 3.16.3)

### To use gcc/g++ on a Macintosh, you must set the Compilers
### here, not inside the project
##if(APPLE)
##       set(CMAKE_C_COMPILER "/usr/local/bin/gcc-7")
##       set(CMAKE_CXX_COMPILER "/usr/local/bin/g++-7")
##endif()
### TODO: for now, we use CLang for Mac
###
### In order to create OpenFHE's static libraries you should enable
### the BUILD_STATIC option. For that, you run "cmake .. -DBUILD_STATIC=ON".
### After having your link completed you will find static libs
### with the suffix "_static" in ./build/libs/.
### Examples: OPENFHEpke_static.a, OPENFHEcore_static.a, etc.
### After you run "make install" in your build directory, you can build your custom application.
### If you need your application to be linked statically, then run "cmake .. -DBUILD_STATIC=ON"

project(fhe_evaluator CXX)
set(CMAKE_CXX_STANDARD 17)
option(BUILD_STATIC "Set to ON to include static versions of the library" OFF)

find_package(OpenFHE CONFIG REQUIRED)
if(OpenFHE_FOUND)
    message(STATUS "FOUND PACKAGE OpenFHE")
    message(STATUS "OpenFHE Version: ${BASE_OPENFHE_VERSION}")
    message(STATUS "OpenFHE installed as shared libraries: ${OpenFHE_SHARED}")
    message(STATUS "OpenFHE include files location: ${OpenFHE_INCLUDE}")
    message(STATUS "OpenFHE lib files location: ${OpenFHE_LIBDIR}")
    message(STATUS "OpenFHE Native Backend size: ${OpenFHE_NATIVE_SIZE}")
else()
    message(FATAL_ERROR "PACKAGE OpenFHE NOT FOUND")
endif()

set(CMAKE_CXX_FLAGS ${OpenFHE_CXX_FLAGS})

include_directories(${OPENMP_INCLUDES})
include_directories(${OpenFHE_INCLUDE})
include_directories(${OpenFHE_INCLUDE}/third-party/include)
include_directories(${OpenFHE_INCLUDE}/core)
include_directories(${OpenFHE_INCLUDE}/pke)
include_directories(${OpenFHE_INCLUDE}/binfhe)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include) #新增smoke测试
### add directories for other OpenFHE modules as needed for your project

if(UNIX AND NOT APPLE)
    add_link_options(-Wl,--no-as-needed)
endif()

link_directories(${OpenFHE_LIBDIR})
link_directories(${OPENMP_LIBRARIES})
if(BUILD_STATIC)
    set(CMAKE_EXE_LINKER_FLAGS "${OpenFHE_EXE_LINKER_FLAGS} -static")
    link_libraries(${OpenFHE_STATIC_LIBRARIES})
else()
    set(CMAKE_EXE_LINKER_FLAGS ${OpenFHE_EXE_LINKER_FLAGS})
    link_libraries(${OpenFHE_SHARED_LIBRARIES})
endif()

### ADD YOUR EXECUTABLE(s) HERE
### add_executable(EXECUTABLE-NAME SOURCES)
###
### EXAMPLE:
### add_executable(test demo-simple-example.cpp)
add_library(smoke_common src/smoke_common.cpp) #新增
target_include_directories(smoke_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include) #新增smoke
add_executable(fhe-evaluator src/main.cpp)
add_executable(ckks_smoke_01_norelin src/smoke/ckks_smoke_01_norelin.cpp)
add_executable(ckks_smoke_02_relin_add_same_scale src/smoke/ckks_smoke_02_relin_add_same_scale.cpp)
add_executable(ckks_smoke_03_rescale_manual src/smoke/ckks_smoke_03_rescale_manual.cpp)
add_executable(ckks_smoke_04_scaling_modes src/smoke/ckks_smoke_04_scaling_modes.cpp)
add_executable(ckks_smoke_05_evalpolyps src/smoke/ckks_smoke_05_evalpolyps.cpp)
add_executable(ckks_smoke_06_mixed_add src/smoke/ckks_smoke_06_mixed_add.cpp)
add_executable(ckks_survival_01_state_trace src/survival/ckks_survival_01_state_trace.cpp)
add_executable(ckks_baseline_01_square_chain src/baseline/ckks_baseline_01_square_chain.cpp)
add_executable(ckks_lazy_01_square_chain src/lazy/ckks_lazy_01_square_chain.cpp)
add_executable(ckks_baseline_02_double_square_aggregate src/baseline/ckks_baseline_02_double_square_aggregate.cpp)
add_executable(ckks_lazy_02_double_square_aggregate src/lazy/ckks_lazy_02_double_square_aggregate.cpp)
add_executable(ckks_baseline_03_poly_block_fold src/baseline/ckks_baseline_03_poly_block_fold.cpp)
add_executable(ckks_lazy_03_poly_block_fold src/lazy/ckks_lazy_03_poly_block_fold.cpp)
add_executable(ckks_evaluator_01_poly_naive src/evaluator/ckks_evaluator_01_poly_naive.cpp)
add_executable(ckks_evaluator_02_poly_lazy src/evaluator/ckks_evaluator_02_poly_lazy.cpp)

target_link_libraries(ckks_smoke_01_norelin PRIVATE smoke_common)
target_link_libraries(ckks_smoke_02_relin_add_same_scale PRIVATE smoke_common)
target_link_libraries(ckks_smoke_03_rescale_manual PRIVATE smoke_common)
target_link_libraries(ckks_smoke_04_scaling_modes PRIVATE smoke_common)
target_link_libraries(ckks_smoke_05_evalpolyps PRIVATE smoke_common)
target_link_libraries(ckks_smoke_06_mixed_add PRIVATE smoke_common)
target_link_libraries(ckks_survival_01_state_trace PRIVATE smoke_common)
target_link_libraries(ckks_baseline_01_square_chain PRIVATE smoke_common)
target_link_libraries(ckks_lazy_01_square_chain PRIVATE smoke_common)
target_link_libraries(ckks_baseline_02_double_square_aggregate PRIVATE smoke_common)
target_link_libraries(ckks_lazy_02_double_square_aggregate PRIVATE smoke_common)
target_link_libraries(ckks_baseline_03_poly_block_fold PRIVATE smoke_common)
target_link_libraries(ckks_lazy_03_poly_block_fold PRIVATE smoke_common)
target_link_libraries(ckks_evaluator_01_poly_naive PRIVATE smoke_common)
target_link_libraries(ckks_evaluator_02_poly_lazy PRIVATE smoke_common)
