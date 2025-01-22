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
