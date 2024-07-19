A single file RDC test program that starts RDC in embedded mode and continuously collect and print GPU metrics.

You will need to install RDC (https://github.com/ROCm/rdc) first before building this program.

On CentOS Stream 9 with RDC installed, this can be built by `g++ ./rdc_test.cpp -I/opt/rocm/include -L/opt/rocm/lib -lrdc_bootstrap -std=c++17 -o rdc_test`.
