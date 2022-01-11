add_library(HeatCFGPrinter MODULE HeatCFGPrinter.cpp HeatUtils.cpp)
add_library(HeatCallPrinter MODULE HeatCallPrinter.cpp HeatUtils.cpp)

target_link_libraries(HeatCFGPrinter LLVM)
target_link_libraries(HeatCallPrinter LLVM)
