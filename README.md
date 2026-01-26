# THIS PROJECT IS ARCHIVED   
Intel will not provide or guarantee development of or support for this project, including but not limited to, maintenance, bug fixes, new releases or updates.  
Patches to this project are no longer accepted by Intel.  
 If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the community, please create your own fork of the project.  
  
# XeSS-VALAR-Demo
Mini-Engine Demonstration of Combining XeSS with VRS Tier 2.

# To build
Open a 'Developer Command Prompt for VS 2022' command window and type the following to build the project:
```
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --config Release
```

# To run
In the same 'Developer Command Prompt for VS 2022', type the following to run the project
```
start build\release\
```

and then double click 'xess_demo.exe'

MIT License
Copyright Intel(R) 2023-2025
