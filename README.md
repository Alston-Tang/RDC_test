## Platform
Ubuntu Desktop 22.04

Install ROCm 6.0.2 via amdgpu-install following instructions https://rocm.docs.amd.com/projects/install-on-linux/en/develop/how-to/amdgpu-install.html
## Build
`g++ pcie_test_amd.cpp -I/opt/rocm/include -D__HIP_PLATFORM_AMD__ -L/opt/rocm/lib -lamdhip64 -o pcie_test`
## Run
`./pcie_test --device 1 --mode=range --htod --iters=50000 --start=100000000 --end=100000000 --increment=1`

These parameters ask the program to transfer data of size 100000000 bytes from host to device 1, repeating 50000 times.
