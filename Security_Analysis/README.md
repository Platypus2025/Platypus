### Security Analysis

This folder contains the scripts for reproducing the results reported in `Table 2` of the paper. For each of the three benchmarks (nginx, redis, sqlite), run the corresponding script. The metric of interest is the percentage reduction in indirectly accessible cross-DSO `endbr64` pads. Note that the figures in the CET column may vary across different configurations, compiler flags, and compiler versions.

Run `produce_all.sh` script **inside this directory**.