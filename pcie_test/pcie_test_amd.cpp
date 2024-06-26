/*
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

/*
 * This is a simple test program to measure the memcopy bandwidth of the GPU.
 * It can measure device to device copy bandwidth, host to device copy bandwidth
 * for pageable and pinned memory, and device to host copy bandwidth for
 * pageable and pinned memory.
 *
 * Usage:
 * ./bandwidthTest [option]...
 */

// CUDA runtime
#include <hip/hip_runtime.h>

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <memory>

#include "helper_string.h"
#include "helper_timer.h"

static const char* sSDKsample = "CUDA Bandwidth Test";

// defines, project
#define MEMCOPY_ITERATIONS 100
#define DEFAULT_SIZE (32 * (1e6)) // 32 M
#define DEFAULT_INCREMENT (4 * (1e6)) // 4 M
#define CACHE_CLEAR_SIZE (16 * (1e6)) // 16 M

// shmoo mode defines
#define SHMOO_MEMSIZE_MAX (64 * (1e6)) // 64 M
#define SHMOO_MEMSIZE_START (1e3) // 1 KB
#define SHMOO_INCREMENT_1KB (1e3) // 1 KB
#define SHMOO_INCREMENT_2KB (2 * 1e3) // 2 KB
#define SHMOO_INCREMENT_10KB (10 * (1e3)) // 10KB
#define SHMOO_INCREMENT_100KB (100 * (1e3)) // 100 KB
#define SHMOO_INCREMENT_1MB (1e6) // 1 MB
#define SHMOO_INCREMENT_2MB (2 * 1e6) // 2 MB
#define SHMOO_INCREMENT_4MB (4 * 1e6) // 4 MB
#define SHMOO_LIMIT_20KB (20 * (1e3)) // 20 KB
#define SHMOO_LIMIT_50KB (50 * (1e3)) // 50 KB
#define SHMOO_LIMIT_100KB (100 * (1e3)) // 100 KB
#define SHMOO_LIMIT_1MB (1e6) // 1 MB
#define SHMOO_LIMIT_16MB (16 * 1e6) // 16 MB
#define SHMOO_LIMIT_32MB (32 * 1e6) // 32 MB

// CPU cache flush
#define FLUSH_SIZE (256 * 1024 * 1024)
char* flush_buf;

// enums, project
enum testMode { QUICK_MODE, RANGE_MODE, SHMOO_MODE };
enum memcpyKind { DEVICE_TO_HOST, HOST_TO_DEVICE, DEVICE_TO_DEVICE };
enum printMode { USER_READABLE, CSV };
enum memoryMode { PINNED, PAGEABLE };

hipError_t error;

template <typename T>
void check(
    T result,
    char const* const func,
    const char* const file,
    int const line) {
  if (result) {
    fprintf(
        stderr,
        "HIP error at %s:%d code=%d(%s) \"%s\" \n",
        file,
        line,
        static_cast<unsigned int>(result),
       	hipGetErrorString(result),
        func);
    exit(EXIT_FAILURE);
  }
}

#define checkHipErrors(val) check((val), #val, __FILE__, __LINE__)

const char* sMemoryCopyKind[] = {
    "Device to Host",
    "Host to Device",
    "Device to Device",
    NULL};

const char* sMemoryMode[] = {"PINNED", "PAGEABLE", NULL};

// if true, use CPU based timing for everything
static bool bDontUseGPUTiming;

int* pArgc = NULL;
char** pArgv = NULL;

inline void do_variable_delay(int delay_ns_min, int delay_ns_max) {
  if (delay_ns_max > 0) {
    int range = delay_ns_max - delay_ns_min;
    if (range > 0) {
      int delay = rand() % range + delay_ns_min;

      const timespec tdelay{0, (long)delay};
      nanosleep(&tdelay, NULL);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// declaration, forward
int runTest(const int argc, const char** argv);
void testBandwidth(
    unsigned int start,
    unsigned int end,
    unsigned int increment,
    unsigned int iters,
    testMode mode,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
void testBandwidthQuick(
    unsigned int size,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
void testBandwidthRange(
    unsigned int start,
    unsigned int end,
    unsigned int increment,
    unsigned int iters,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
void testBandwidthShmoo(
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
float testDeviceToHostTransfer(
    unsigned int memSize,
    unsigned int iters,
    memoryMode memMode,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
float testHostToDeviceTransfer(
    unsigned int memSize,
    unsigned int iters,
    memoryMode memMode,
    bool wc,
    int delay_ns_min = 0,
    int delay_ns_max = 0);
float testDeviceToDeviceTransfer(unsigned int memSize, unsigned int iters);
void printResultsReadable(
    unsigned int* memSizes,
    double* bandwidths,
    unsigned int count,
    memcpyKind kind,
    memoryMode memMode,
    int iNumDevs,
    bool wc);
void printResultsCSV(
    unsigned int* memSizes,
    double* bandwidths,
    unsigned int count,
    memcpyKind kind,
    memoryMode memMode,
    int iNumDevs,
    bool wc);
void printHelp(void);

////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv) {
  pArgc = &argc;
  pArgv = argv;

  flush_buf = (char*)malloc(FLUSH_SIZE);

  // set logfile name and start logs
  printf("[%s] - Starting...\n", sSDKsample);

  int iRetVal = runTest(argc, (const char**)argv);

  if (iRetVal < 0) {
    checkHipErrors(hipSetDevice(0));
  }

  // finish
  printf("%s\n", (iRetVal == 0) ? "Result = PASS" : "Result = FAIL");

  printf(
      "\nNOTE: The CUDA Samples are not meant for performance measurements. "
      "Results may vary when GPU Boost is enabled.\n");

  free(flush_buf);

  exit((iRetVal == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}

///////////////////////////////////////////////////////////////////////////////
// Parse args, run the appropriate tests
///////////////////////////////////////////////////////////////////////////////
int runTest(const int argc, const char** argv) {
  int start = (int)DEFAULT_SIZE;
  int end = (int)DEFAULT_SIZE;
  int startDevice = -1;
  int endDevice = -1;
  int increment = (int)DEFAULT_INCREMENT;
  int iters = (int)MEMCOPY_ITERATIONS;
  int delay_ns_min = 0;
  int delay_ns_max = 0;
  testMode mode = QUICK_MODE;
  bool htod = false;
  bool dtoh = false;
  bool dtod = false;
  bool wc = false;
  char* modeStr;
  char* device = NULL;
  printMode printmode = USER_READABLE;
  char* memModeStr = NULL;
  memoryMode memMode = PINNED;

  // process command line args
  if (checkCmdLineFlag(argc, argv, "help")) {
    printHelp();
    return 0;
  }

  if (checkCmdLineFlag(argc, argv, "csv")) {
    printmode = CSV;
  }

  if (getCmdLineArgumentString(argc, argv, "memory", &memModeStr)) {
    if (strcmp(memModeStr, "pageable") == 0) {
      memMode = PAGEABLE;
    } else if (strcmp(memModeStr, "pinned") == 0) {
      memMode = PINNED;
    } else {
      printf("Invalid memory mode - valid modes are pageable or pinned\n");
      printf("See --help for more information\n");
      return -1000;
    }
  } else {
    // default - pinned memory
    memMode = PINNED;
  }

  if (getCmdLineArgumentString(argc, argv, "device", &device)) {
    int deviceCount;
    hipError_t error_id = hipGetDeviceCount(&deviceCount);

    if (error_id != hipSuccess) {
      printf(
          "hipGetDeviceCount returned %d\n-> %s\n",
          (int)error_id,
          hipGetErrorString(error_id));
      exit(EXIT_FAILURE);
    }

    if (deviceCount == 0) {
      printf("!!!!!No devices found!!!!!\n");
      return -2000;
    }

    if (strcmp(device, "all") == 0) {
      printf(
          "\n!!!!!Cumulative Bandwidth to be computed from all the devices "
          "!!!!!!\n\n");
      startDevice = 0;
      endDevice = deviceCount - 1;
    } else {
      startDevice = endDevice = atoi(device);

      if (startDevice >= deviceCount || startDevice < 0) {
        printf(
            "\n!!!!!Invalid GPU number %d given hence default gpu %d will be "
            "used !!!!!\n",
            startDevice,
            0);
        startDevice = endDevice = 0;
      }
    }
  }

  printf("Running on...\n\n");

  if (startDevice == -1) {
    // No device specified. Treat here also the MIG instance cases
    int num_devices = -1;
    checkHipErrors(hipGetDeviceCount(&num_devices));
    if (num_devices <= 0) {
      fprintf(stderr, "No HIP devices found!\n");
      return -2000;
    } else {
      fprintf(
          stderr,
          "No HIP device index specified. Running on the first available device.\n");
    }

    // Else do nothing. Currently hipSetDevice does not work with MIG
    // UUID so we are just running on the currently available device.
  } else {
    for (int currentDevice = startDevice; currentDevice <= endDevice;
         currentDevice++) {
      hipDeviceProp_t deviceProp;
      hipError_t error_id =
          hipGetDeviceProperties(&deviceProp, currentDevice);

      if (error_id == hipSuccess) {
        printf(" Device %d: %s\n", currentDevice, deviceProp.name);

        if (deviceProp.computeMode == hipComputeModeProhibited) {
          fprintf(
              stderr,
              "Error: device is running in <Compute Mode Prohibited>, no "
              "threads can use ::hipSetDevice().\n");
          checkHipErrors(hipSetDevice(currentDevice));

          exit(EXIT_FAILURE);
        }
      } else {
        printf(
            "hipGetDeviceProperties returned %d\n-> %s\n",
            (int)error_id,
            hipGetErrorString(error_id));
        checkHipErrors(hipSetDevice(currentDevice));

        exit(EXIT_FAILURE);
      }
    }
  }

  if (getCmdLineArgumentString(argc, argv, "mode", &modeStr)) {
    // figure out the mode
    if (strcmp(modeStr, "quick") == 0) {
      printf(" Quick Mode\n\n");
      mode = QUICK_MODE;
    } else if (strcmp(modeStr, "shmoo") == 0) {
      printf(" Shmoo Mode\n\n");
      mode = SHMOO_MODE;
    } else if (strcmp(modeStr, "range") == 0) {
      printf(" Range Mode\n\n");
      mode = RANGE_MODE;
    } else {
      printf("Invalid mode - valid modes are quick, range, or shmoo\n");
      printf("See --help for more information\n");
      return -3000;
    }
  } else {
    // default mode - quick
    printf(" Quick Mode\n\n");
    mode = QUICK_MODE;
  }

  if (checkCmdLineFlag(argc, argv, "htod")) {
    htod = true;
  }

  if (checkCmdLineFlag(argc, argv, "dtoh")) {
    dtoh = true;
  }

  if (checkCmdLineFlag(argc, argv, "dtod")) {
    dtod = true;
  }

  if (checkCmdLineFlag(argc, argv, "cputiming")) {
    bDontUseGPUTiming = true;
  }

  if (checkCmdLineFlag(argc, argv, "iters")) {
    iters = getCmdLineArgumentInt(argc, argv, "iters");

    if (iters <= 0) {
      printf("Illegal argument - iters must be greater than zero\n");
      return -9000;
    }
  }

  if (checkCmdLineFlag(argc, argv, "delay_ns_min")) {
    delay_ns_min = getCmdLineArgumentInt(argc, argv, "delay_ns_min");

    if (delay_ns_min < 0) {
      printf(
          "Illegal argument - delay_ns_min must be greater or equal than zero\n");
      return -9000;
    }
  }

  if (checkCmdLineFlag(argc, argv, "delay_ns_max")) {
    delay_ns_max = getCmdLineArgumentInt(argc, argv, "delay_ns_max");

    if (delay_ns_max < 0) {
      printf(
          "Illegal argument - delay_ns_max must be greater or equal than zero\n");
      return -9000;
    }
  }

  if (!htod && !dtoh && !dtod) {
    // default:  All
    htod = true;
    dtoh = true;
    dtod = true;
  }

  if (RANGE_MODE == mode) {
    if (checkCmdLineFlag(argc, (const char**)argv, "start")) {
      start = getCmdLineArgumentInt(argc, argv, "start");

      if (start <= 0) {
        printf("Illegal argument - start must be greater than zero\n");
        return -4000;
      }
    } else {
      printf("Must specify a starting size in range mode\n");
      printf("See --help for more information\n");
      return -5000;
    }

    if (checkCmdLineFlag(argc, (const char**)argv, "end")) {
      end = getCmdLineArgumentInt(argc, argv, "end");

      if (end <= 0) {
        printf("Illegal argument - end must be greater than zero\n");
        return -6000;
      }

      if (start > end) {
        printf("Illegal argument - start is greater than end\n");
        return -7000;
      }
    } else {
      printf("Must specify an end size in range mode.\n");
      printf("See --help for more information\n");
      return -8000;
    }

    if (checkCmdLineFlag(argc, argv, "increment")) {
      increment = getCmdLineArgumentInt(argc, argv, "increment");

      if (increment <= 0) {
        printf("Illegal argument - increment must be greater than zero\n");
        return -9000;
      }
    } else {
      printf("Must specify an increment in user mode\n");
      printf("See --help for more information\n");
      return -10000;
    }
  }

  if (htod) {
    testBandwidth(
        (unsigned int)start,
        (unsigned int)end,
        (unsigned int)increment,
        (unsigned int)iters,
        mode,
        HOST_TO_DEVICE,
        printmode,
        memMode,
        startDevice,
        endDevice,
        wc,
        delay_ns_min,
        delay_ns_max);
  }

  if (dtoh) {
    testBandwidth(
        (unsigned int)start,
        (unsigned int)end,
        (unsigned int)increment,
        (unsigned int)iters,
        mode,
        DEVICE_TO_HOST,
        printmode,
        memMode,
        startDevice,
        endDevice,
        wc,
        delay_ns_min,
        delay_ns_max);
  }

  if (dtod) {
    testBandwidth(
        (unsigned int)start,
        (unsigned int)end,
        (unsigned int)increment,
        (unsigned int)iters,
        mode,
        DEVICE_TO_DEVICE,
        printmode,
        memMode,
        startDevice,
        endDevice,
        wc);
  }

  // Ensure that we reset all CUDA Devices in question
  for (int nDevice = startDevice; nDevice <= endDevice; nDevice++) {
    checkHipErrors(hipSetDevice(nDevice));
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
//  Run a bandwidth test
///////////////////////////////////////////////////////////////////////////////
void testBandwidth(
    unsigned int start,
    unsigned int end,
    unsigned int increment,
    unsigned int iters,
    testMode mode,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  switch (mode) {
    case QUICK_MODE:
      testBandwidthQuick(
          (unsigned int)DEFAULT_SIZE,
          kind,
          printmode,
          memMode,
          startDevice,
          endDevice,
          wc,
          delay_ns_min,
          delay_ns_max);
      break;

    case RANGE_MODE:
      testBandwidthRange(
          start,
          end,
          increment,
          iters,
          kind,
          printmode,
          memMode,
          startDevice,
          endDevice,
          wc,
          delay_ns_min,
          delay_ns_max);
      break;

    case SHMOO_MODE:
      testBandwidthShmoo(
          kind,
          printmode,
          memMode,
          startDevice,
          endDevice,
          wc,
          delay_ns_min,
          delay_ns_max);
      break;

    default:
      break;
  }
}

//////////////////////////////////////////////////////////////////////
//  Run a quick mode bandwidth test
//////////////////////////////////////////////////////////////////////
void testBandwidthQuick(
    unsigned int size,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  testBandwidthRange(
      size,
      size,
      (unsigned int)DEFAULT_INCREMENT,
      (unsigned int)MEMCOPY_ITERATIONS,
      kind,
      printmode,
      memMode,
      startDevice,
      endDevice,
      wc,
      delay_ns_min,
      delay_ns_max);
}

///////////////////////////////////////////////////////////////////////
//  Run a range mode bandwidth test
//////////////////////////////////////////////////////////////////////
void testBandwidthRange(
    unsigned int start,
    unsigned int end,
    unsigned int increment,
    unsigned int iters,
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  // count the number of copies we're going to run
  unsigned int count = 1 + ((end - start) / increment);

  unsigned int* memSizes = (unsigned int*)malloc(count * sizeof(unsigned int));
  double* bandwidths = (double*)malloc(count * sizeof(double));

  // Before calculating the cumulative bandwidth, initialize bandwidths array to
  // NULL
  for (unsigned int i = 0; i < count; i++) {
    bandwidths[i] = 0.0;
  }

  // Use the device asked by the user
  for (int currentDevice = startDevice; currentDevice <= endDevice;
       currentDevice++) {
    checkHipErrors(hipSetDevice(currentDevice));

    // run each of the copies
    for (unsigned int i = 0; i < count; i++) {
      memSizes[i] = start + i * increment;

      switch (kind) {
        case DEVICE_TO_HOST:
          bandwidths[i] += testDeviceToHostTransfer(
              memSizes[i], iters, memMode, wc, delay_ns_min, delay_ns_max);
          break;

        case HOST_TO_DEVICE:
          bandwidths[i] += testHostToDeviceTransfer(
              memSizes[i], iters, memMode, wc, delay_ns_min, delay_ns_max);
          break;

        case DEVICE_TO_DEVICE:
          bandwidths[i] += testDeviceToDeviceTransfer(memSizes[i], iters);
          break;
      }
    }
  } // Complete the bandwidth computation on all the devices

  // print results
  if (printmode == CSV) {
    printResultsCSV(
        memSizes,
        bandwidths,
        count,
        kind,
        memMode,
        (1 + endDevice - startDevice),
        wc);
  } else {
    printResultsReadable(
        memSizes,
        bandwidths,
        count,
        kind,
        memMode,
        (1 + endDevice - startDevice),
        wc);
  }

  // clean up
  free(memSizes);
  free(bandwidths);
}

//////////////////////////////////////////////////////////////////////////////
// Intense shmoo mode - covers a large range of values with varying increments
//////////////////////////////////////////////////////////////////////////////
void testBandwidthShmoo(
    memcpyKind kind,
    printMode printmode,
    memoryMode memMode,
    int startDevice,
    int endDevice,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  // count the number of copies to make
  unsigned int count = 1 +
      (unsigned int)(SHMOO_LIMIT_20KB / SHMOO_INCREMENT_1KB) +
      (unsigned int)((SHMOO_LIMIT_50KB - SHMOO_LIMIT_20KB) /
                     SHMOO_INCREMENT_2KB) +
      (unsigned int)((SHMOO_LIMIT_100KB - SHMOO_LIMIT_50KB) /
                     SHMOO_INCREMENT_10KB) +
      (unsigned int)((SHMOO_LIMIT_1MB - SHMOO_LIMIT_100KB) /
                     SHMOO_INCREMENT_100KB) +
      (unsigned int)((SHMOO_LIMIT_16MB - SHMOO_LIMIT_1MB) /
                     SHMOO_INCREMENT_1MB) +
      (unsigned int)((SHMOO_LIMIT_32MB - SHMOO_LIMIT_16MB) /
                     SHMOO_INCREMENT_2MB) +
      (unsigned int)((SHMOO_MEMSIZE_MAX - SHMOO_LIMIT_32MB) /
                     SHMOO_INCREMENT_4MB);

  unsigned int* memSizes = (unsigned int*)malloc(count * sizeof(unsigned int));
  double* bandwidths = (double*)malloc(count * sizeof(double));

  // Before calculating the cumulative bandwidth, initialize bandwidths array to
  // NULL
  for (unsigned int i = 0; i < count; i++) {
    bandwidths[i] = 0.0;
  }

  // Use the device asked by the user
  for (int currentDevice = startDevice; currentDevice <= endDevice;
       currentDevice++) {
    checkHipErrors(hipSetDevice(currentDevice));
    // Run the shmoo
    int iteration = 0;
    unsigned int memSize = 0;

    while (memSize <= SHMOO_MEMSIZE_MAX) {
      if (memSize < SHMOO_LIMIT_20KB) {
        memSize += (unsigned int)SHMOO_INCREMENT_1KB;
      } else if (memSize < SHMOO_LIMIT_50KB) {
        memSize += (unsigned int)SHMOO_INCREMENT_2KB;
      } else if (memSize < SHMOO_LIMIT_100KB) {
        memSize += (unsigned int)SHMOO_INCREMENT_10KB;
      } else if (memSize < SHMOO_LIMIT_1MB) {
        memSize += (unsigned int)SHMOO_INCREMENT_100KB;
      } else if (memSize < SHMOO_LIMIT_16MB) {
        memSize += (unsigned int)SHMOO_INCREMENT_1MB;
      } else if (memSize < SHMOO_LIMIT_32MB) {
        memSize += (unsigned int)SHMOO_INCREMENT_2MB;
      } else {
        memSize += (unsigned int)SHMOO_INCREMENT_4MB;
      }

      memSizes[iteration] = memSize;

      switch (kind) {
        case DEVICE_TO_HOST:
          bandwidths[iteration] += testDeviceToHostTransfer(
              memSizes[iteration],
              MEMCOPY_ITERATIONS,
              memMode,
              wc,
              delay_ns_min,
              delay_ns_max);
          break;

        case HOST_TO_DEVICE:
          bandwidths[iteration] += testHostToDeviceTransfer(
              memSizes[iteration],
              MEMCOPY_ITERATIONS,
              memMode,
              wc,
              delay_ns_min,
              delay_ns_max);
          break;

        case DEVICE_TO_DEVICE:
          bandwidths[iteration] += testDeviceToDeviceTransfer(
              memSizes[iteration], MEMCOPY_ITERATIONS);
          break;
      }

      iteration++;
      printf(".");
      fflush(0);
    }
  } // Complete the bandwidth computation on all the devices

  // print results
  printf("\n");

  if (CSV == printmode) {
    printResultsCSV(
        memSizes,
        bandwidths,
        count,
        kind,
        memMode,
        (1 + endDevice - startDevice),
        wc);
  } else {
    printResultsReadable(
        memSizes,
        bandwidths,
        count,
        kind,
        memMode,
        (1 + endDevice - startDevice),
        wc);
  }

  // clean up
  free(memSizes);
  free(bandwidths);
}

///////////////////////////////////////////////////////////////////////////////
//  test the bandwidth of a device to host memcopy of a specific size
///////////////////////////////////////////////////////////////////////////////
float testDeviceToHostTransfer(
    unsigned int memSize,
    unsigned int iters,
    memoryMode memMode,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  StopWatchInterface* timer = NULL;
  float elapsedTimeInMs = 0.0f;
  float bandwidthInGBs = 0.0f;
  unsigned char* h_idata = NULL;
  unsigned char* h_odata = NULL;
  hipEvent_t start, stop;

  sdkCreateTimer(&timer);
  checkHipErrors(hipEventCreate(&start));
  checkHipErrors(hipEventCreate(&stop));

  // allocate host memory
  if (PINNED == memMode) {
    // pinned memory mode - use special function to get OS-pinned memory
#if CUDART_VERSION >= 2020
    checkHipErrors(hipHostAlloc(
        (void**)&h_idata, memSize, (wc) ? hipHostMallocWriteCombined : 0));
    checkHipErrors(hipHostAlloc(
        (void**)&h_odata, memSize, (wc) ? hipHostMallocWriteCombined : 0));
#else
    checkHipErrors(hipHostMalloc((void**)&h_idata, memSize));
    checkHipErrors(hipHostMalloc((void**)&h_odata, memSize));
#endif
  } else {
    // pageable memory mode - use malloc
    h_idata = (unsigned char*)malloc(memSize);
    h_odata = (unsigned char*)malloc(memSize);

    if (h_idata == 0 || h_odata == 0) {
      fprintf(stderr, "Not enough memory avaialable on host to run test!\n");
      exit(EXIT_FAILURE);
    }
  }

  // initialize the memory
  for (unsigned int i = 0; i < memSize / sizeof(unsigned char); i++) {
    h_idata[i] = (unsigned char)(i & 0xff);
  }

  // allocate device memory
  unsigned char* d_idata;
  checkHipErrors(hipMalloc((void**)&d_idata, memSize));

  // initialize the device memory
  checkHipErrors(
      hipMemcpy(d_idata, h_idata, memSize, hipMemcpyHostToDevice));

  // copy data from GPU to Host
  if (PINNED == memMode) {
    if (bDontUseGPUTiming)
      sdkStartTimer(&timer);
    checkHipErrors(hipEventRecord(start, 0));
    for (unsigned int i = 0; i < iters; i++) {
      checkHipErrors(hipMemcpyAsync(
          h_odata, d_idata, memSize, hipMemcpyDeviceToHost, 0));
      do_variable_delay(delay_ns_min, delay_ns_max);
    }
    checkHipErrors(hipEventRecord(stop, 0));
    checkHipErrors(hipDeviceSynchronize());
    checkHipErrors(hipEventElapsedTime(&elapsedTimeInMs, start, stop));
    if (bDontUseGPUTiming) {
      sdkStopTimer(&timer);
      elapsedTimeInMs = sdkGetTimerValue(&timer);
      sdkResetTimer(&timer);
    }
  } else {
    elapsedTimeInMs = 0;
    for (unsigned int i = 0; i < iters; i++) {
      sdkStartTimer(&timer);
      checkHipErrors(
          hipMemcpy(h_odata, d_idata, memSize, hipMemcpyDeviceToHost));
      do_variable_delay(delay_ns_min, delay_ns_max);
      sdkStopTimer(&timer);
      elapsedTimeInMs += sdkGetTimerValue(&timer);
      sdkResetTimer(&timer);
      memset(flush_buf, i, FLUSH_SIZE);
    }
  }

  // calculate bandwidth in GB/s
  float time_s = elapsedTimeInMs / (float)1e3;
  bandwidthInGBs = (memSize * (float)iters) / (float)1e9;
  bandwidthInGBs = bandwidthInGBs / time_s;
  // clean up memory
  checkHipErrors(hipEventDestroy(stop));
  checkHipErrors(hipEventDestroy(start));
  sdkDeleteTimer(&timer);

  if (PINNED == memMode) {
    checkHipErrors(hipHostFree(h_idata));
    checkHipErrors(hipHostFree(h_odata));
  } else {
    free(h_idata);
    free(h_odata);
  }

  checkHipErrors(hipFree(d_idata));

  return bandwidthInGBs;
}

///////////////////////////////////////////////////////////////////////////////
//! test the bandwidth of a host to device memcopy of a specific size
///////////////////////////////////////////////////////////////////////////////
float testHostToDeviceTransfer(
    unsigned int memSize,
    unsigned int iters,
    memoryMode memMode,
    bool wc,
    int delay_ns_min,
    int delay_ns_max) {
  StopWatchInterface* timer = NULL;
  float elapsedTimeInMs = 0.0f;
  float bandwidthInGBs = 0.0f;
  hipEvent_t start, stop;
  sdkCreateTimer(&timer);
  checkHipErrors(hipEventCreate(&start));
  checkHipErrors(hipEventCreate(&stop));

  // allocate host memory
  unsigned char* h_odata = NULL;

  if (PINNED == memMode) {
    // pinned memory mode - use special function to get OS-pinned memory
    checkHipErrors(hipHostMalloc((void**)&h_odata, memSize));
  } else {
    // pageable memory mode - use malloc
    h_odata = (unsigned char*)malloc(memSize);

    if (h_odata == 0) {
      fprintf(stderr, "Not enough memory available on host to run test!\n");
      exit(EXIT_FAILURE);
    }
  }

  unsigned char* h_cacheClear1 =
      (unsigned char*)malloc((size_t)CACHE_CLEAR_SIZE);
  unsigned char* h_cacheClear2 =
      (unsigned char*)malloc((size_t)CACHE_CLEAR_SIZE);

  if (h_cacheClear1 == 0 || h_cacheClear2 == 0) {
    fprintf(stderr, "Not enough memory available on host to run test!\n");
    exit(EXIT_FAILURE);
  }

  // initialize the memory
  for (unsigned int i = 0; i < memSize / sizeof(unsigned char); i++) {
    h_odata[i] = (unsigned char)(i & 0xff);
  }

  for (unsigned int i = 0; i < CACHE_CLEAR_SIZE / sizeof(unsigned char); i++) {
    h_cacheClear1[i] = (unsigned char)(i & 0xff);
    h_cacheClear2[i] = (unsigned char)(0xff - (i & 0xff));
  }

  // allocate device memory
  unsigned char* d_idata;
  checkHipErrors(hipMalloc((void**)&d_idata, memSize));

  // copy host memory to device memory
  if (PINNED == memMode) {
    if (bDontUseGPUTiming)
      sdkStartTimer(&timer);
    checkHipErrors(hipEventRecord(start, 0));
    for (unsigned int i = 0; i < iters; i++) {
      checkHipErrors(hipMemcpyAsync(
          d_idata, h_odata, memSize, hipMemcpyHostToDevice, 0));
      do_variable_delay(delay_ns_min, delay_ns_max);
    }
    checkHipErrors(hipEventRecord(stop, 0));
    checkHipErrors(hipDeviceSynchronize());
    checkHipErrors(hipEventElapsedTime(&elapsedTimeInMs, start, stop));
    if (bDontUseGPUTiming) {
      sdkStopTimer(&timer);
      elapsedTimeInMs = sdkGetTimerValue(&timer);
      sdkResetTimer(&timer);
    }
  } else {
    elapsedTimeInMs = 0;
    for (unsigned int i = 0; i < iters; i++) {
      sdkStartTimer(&timer);
      checkHipErrors(
          hipMemcpy(d_idata, h_odata, memSize, hipMemcpyHostToDevice));
      do_variable_delay(delay_ns_min, delay_ns_max);
      sdkStopTimer(&timer);
      elapsedTimeInMs += sdkGetTimerValue(&timer);
      sdkResetTimer(&timer);
      memset(flush_buf, i, FLUSH_SIZE);
    }
  }

  // calculate bandwidth in GB/s
  float time_s = elapsedTimeInMs / (float)1e3;
  bandwidthInGBs = (memSize * (float)iters) / (float)1e9;
  bandwidthInGBs = bandwidthInGBs / time_s;
  // clean up memory
  checkHipErrors(hipEventDestroy(stop));
  checkHipErrors(hipEventDestroy(start));
  sdkDeleteTimer(&timer);

  if (PINNED == memMode) {
    checkHipErrors(hipHostFree(h_odata));
  } else {
    free(h_odata);
  }

  free(h_cacheClear1);
  free(h_cacheClear2);
  checkHipErrors(hipFree(d_idata));

  return bandwidthInGBs;
}

///////////////////////////////////////////////////////////////////////////////
//! test the bandwidth of a device to device memcopy of a specific size
///////////////////////////////////////////////////////////////////////////////
float testDeviceToDeviceTransfer(unsigned int memSize, unsigned int iters) {
  StopWatchInterface* timer = NULL;
  float elapsedTimeInMs = 0.0f;
  float bandwidthInGBs = 0.0f;
  hipEvent_t start, stop;

  sdkCreateTimer(&timer);
  checkHipErrors(hipEventCreate(&start));
  checkHipErrors(hipEventCreate(&stop));

  // allocate host memory
  unsigned char* h_idata = (unsigned char*)malloc(memSize);

  if (h_idata == 0) {
    fprintf(stderr, "Not enough memory avaialable on host to run test!\n");
    exit(EXIT_FAILURE);
  }

  // initialize the host memory
  for (unsigned int i = 0; i < memSize / sizeof(unsigned char); i++) {
    h_idata[i] = (unsigned char)(i & 0xff);
  }

  // allocate device memory
  unsigned char* d_idata;
  checkHipErrors(hipMalloc((void**)&d_idata, memSize));
  unsigned char* d_odata;
  checkHipErrors(hipMalloc((void**)&d_odata, memSize));

  // initialize memory
  checkHipErrors(
      hipMemcpy(d_idata, h_idata, memSize, hipMemcpyHostToDevice));

  // run the memcopy
  sdkStartTimer(&timer);
  checkHipErrors(hipEventRecord(start, 0));

  for (unsigned int i = 0; i < iters; i++) {
    checkHipErrors(
        hipMemcpy(d_odata, d_idata, memSize, hipMemcpyDeviceToDevice));
  }

  checkHipErrors(hipEventRecord(stop, 0));

  // Since device to device memory copies are non-blocking,
  // hipDeviceSynchronize() is required in order to get
  // proper timing.
  checkHipErrors(hipDeviceSynchronize());

  // get the total elapsed time in ms
  sdkStopTimer(&timer);
  checkHipErrors(hipEventElapsedTime(&elapsedTimeInMs, start, stop));

  if (bDontUseGPUTiming) {
    elapsedTimeInMs = sdkGetTimerValue(&timer);
  }

  // calculate bandwidth in GB/s
  float time_s = elapsedTimeInMs / (float)1e3;
  bandwidthInGBs = (2.0f * memSize * (float)iters) / (float)1e9;
  bandwidthInGBs = bandwidthInGBs / time_s;

  // clean up memory
  sdkDeleteTimer(&timer);
  free(h_idata);
  checkHipErrors(hipEventDestroy(stop));
  checkHipErrors(hipEventDestroy(start));
  checkHipErrors(hipFree(d_idata));
  checkHipErrors(hipFree(d_odata));

  return bandwidthInGBs;
}

/////////////////////////////////////////////////////////
// print results in an easily read format
////////////////////////////////////////////////////////
void printResultsReadable(
    unsigned int* memSizes,
    double* bandwidths,
    unsigned int count,
    memcpyKind kind,
    memoryMode memMode,
    int iNumDevs,
    bool wc) {
  printf(" %s Bandwidth, %i Device(s)\n", sMemoryCopyKind[kind], iNumDevs);
  printf(" %s Memory Transfers\n", sMemoryMode[memMode]);

  if (wc) {
    printf(" Write-Combined Memory Writes are Enabled");
  }

  printf("   Transfer Size (Bytes)\tBandwidth(GB/s)\n");
  unsigned int i;

  for (i = 0; i < (count - 1); i++) {
    printf(
        "   %u\t\t\t%s%.1f\n",
        memSizes[i],
        (memSizes[i] < 10000) ? "\t" : "",
        bandwidths[i]);
  }

  printf(
      "   %u\t\t\t%s%.1f\n\n",
      memSizes[i],
      (memSizes[i] < 10000) ? "\t" : "",
      bandwidths[i]);
}

///////////////////////////////////////////////////////////////////////////
// print results in a database format
///////////////////////////////////////////////////////////////////////////
void printResultsCSV(
    unsigned int* memSizes,
    double* bandwidths,
    unsigned int count,
    memcpyKind kind,
    memoryMode memMode,
    int iNumDevs,
    bool wc) {
  std::string sConfig;

  // log config information
  if (kind == DEVICE_TO_DEVICE) {
    sConfig += "D2D";
  } else {
    if (kind == DEVICE_TO_HOST) {
      sConfig += "D2H";
    } else if (kind == HOST_TO_DEVICE) {
      sConfig += "H2D";
    }

    if (memMode == PAGEABLE) {
      sConfig += "-Paged";
    } else if (memMode == PINNED) {
      sConfig += "-Pinned";

      if (wc) {
        sConfig += "-WriteCombined";
      }
    }
  }

  unsigned int i;
  double dSeconds = 0.0;

  for (i = 0; i < count; i++) {
    dSeconds = (double)memSizes[i] / (bandwidths[i] * (double)(1e9));
    printf(
        "bandwidthTest-%s, Bandwidth = %.1f GB/s, Time = %.5f s, Size = %u "
        "bytes, NumDevsUsed = %d\n",
        sConfig.c_str(),
        bandwidths[i],
        dSeconds,
        memSizes[i],
        iNumDevs);
  }
}

///////////////////////////////////////////////////////////////////////////
// Print help screen
///////////////////////////////////////////////////////////////////////////
void printHelp(void) {
  printf("Usage:  bandwidthTest [OPTION]...\n");
  printf(
      "Test the bandwidth for device to host, host to device, and device to "
      "device transfers\n");
  printf("\n");
  printf(
      "Example:  measure the bandwidth of device to host pinned memory copies "
      "in the range 1024 Bytes to 102400 Bytes in 1024 Byte increments\n");
  printf(
      "./bandwidthTest --memory=pinned --mode=range --start=1024 --end=102400 "
      "--increment=1024 --dtoh\n");

  printf("\n");
  printf("Options:\n");
  printf("--help\tDisplay this help menu\n");
  printf("--csv\tPrint results as a CSV\n");
  printf("--device=[deviceno]\tSpecify the device device to be used\n");
  printf("  all - compute cumulative bandwidth on all the devices\n");
  printf("  0,1,2,...,n - Specify any particular device to be used\n");
  printf("--memory=[MEMMODE]\tSpecify which memory mode to use\n");
  printf("  pageable - pageable memory\n");
  printf("  pinned   - non-pageable system memory\n");
  printf("--mode=[MODE]\tSpecify the mode to use\n");
  printf("  quick - performs a quick measurement\n");
  printf("  range - measures a user-specified range of values\n");
  printf("  shmoo - performs an intense shmoo of a large range of values\n");

  printf("--htod\tMeasure host to device transfers\n");
  printf("--dtoh\tMeasure device to host transfers\n");
  printf("--dtod\tMeasure device to device transfers\n");
#if CUDART_VERSION >= 2020
  printf("--wc\tAllocate pinned memory as write-combined\n");
#endif
  printf("--cputiming\tForce CPU-based timing always\n");
  printf("--iters=[VALUE]\tNumber of times to loop on each memcopy\n");
  printf("Range mode options\n");
  printf("--start=[SIZE]\tStarting transfer size in bytes\n");
  printf("--end=[SIZE]\tEnding transfer size in bytes\n");
  printf("--increment=[SIZE]\tIncrement size in bytes\n");
  printf(
      "--delay_ns_min=[VALUE]\tMinimum delay between transfers in us. Default = 0.\n");
  printf(
      "--delay_ns_max=[VALUE]\tMaximum delay between transfers in us. Default = 0.\n");
}
