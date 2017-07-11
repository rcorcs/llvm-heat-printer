# LLVM Heat Printer

LLVM Heat Printer provides visualization assistance for profiling.
It implements analysis passes that generates visualization (dot) files that depicts the (profiled) execution frequency of a piece of code using a cool/warm color map.

Cool/Warm color map:
![CoolWarm Map](https://github.com/rcorcs/llvm-heat-printer/raw/master/images/coolwarm.png)

## Heat CFG Printer

Generates the heat map of the CFG (control-flow graph) based on the basic block frequency.
The output may include (or not) the LLVM code for each basic block.

<p align="center">
<img src="https://github.com/rcorcs/llvm-heat-printer/raw/master/images/heat-cfg.png" width="250">
<img src="https://github.com/rcorcs/llvm-heat-printer/raw/master/images/heat-cfg-only.png" width="250">
</p>

The user can also choose between a intra-function or inter-function maximum frequency reference.
For the intra-function heat map, activated with the flag '-heat-cfg-per-function', the heat scale will consider
only the frequencies of the basic blocks inside the current function, i.e., every function will have a basic block with maximum heat.
For the inter-function heat map (default), the heat scale will consider all functions of the current module (translation unit),
i.e., it first computes the maximum frequency for all basic blocks in the whole module, such that the heat of each basic block
will be scaled in respect of that maximum frequency.
With the inter-function heat map, the CFGs for some functions can be completely cold.

## Heat CallGraph Printer

Generates the heat map of the call-graph based on either the profiled number of calls or the maximum basic block frequency inside each function.
The following figure illustrates the heat call-graph highlighting the maximum basic block frequency inside each function.

<p align="center">
<img src="https://github.com/rcorcs/llvm-heat-printer/raw/master/images/heat-callgraph.png" width="512">
</p>
