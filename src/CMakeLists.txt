add_library(HeatCFGPrinter MODULE HeatCFGPrinter.cpp HeatUtils.cpp)
add_library(HeatCallPrinter MODULE HeatCallPrinter.cpp HeatUtils.cpp)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(HeatCFGPrinter LLVM)
target_link_libraries(HeatCallPrinter LLVM)
