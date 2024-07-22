//===- test.cpp -------------------------------------------000---*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

constexpr int IN_SIZE = 32;
constexpr int OUT_SIZE = 64;

#define IN_DATATYPE int8_t
#define OUT_DATATYPE int8_t

std::vector<uint32_t> load_instr_sequence(std::string instr_path) {
  std::ifstream instr_file(instr_path);
  std::string line;
  std::vector<uint32_t> instr_v;
  while (std::getline(instr_file, line)) {
    std::istringstream iss(line);
    uint32_t a;
    if (!(iss >> std::hex >> a)) {
      throw std::runtime_error("Unable to parse instruction file\n");
    }
    instr_v.push_back(a);
  }
  return instr_v;
}

int main(int argc, const char *argv[]) {
  std::vector<uint32_t> instr_v = load_instr_sequence("insts.txt");

  // Start the XRT test code
  // Get a device handle
  unsigned int device_index = 0;
  auto device = xrt::device(device_index);

  // Load the xclbin
  auto xclbin = xrt::xclbin("aie.xclbin");

  std::string Node = "MLIR_AIE";

  // Get the kernel from the xclbin
  auto xkernels = xclbin.get_kernels();
  auto xkernel = *std::find_if(xkernels.begin(), xkernels.end(),
                               [Node](xrt::xclbin::kernel &k) {
                                 auto name = k.get_name();
                                 std::cout << "Name: " << name << std::endl;
                                 return name.rfind(Node, 0) == 0;
                               });
  auto kernelName = xkernel.get_name();

  device.register_xclbin(xclbin);

  // get a hardware context
  xrt::hw_context context(device, xclbin.get_uuid());

  // get a kernel handle
  auto kernel = xrt::kernel(context, kernelName);

  auto bo_instr = xrt::bo(device, instr_v.size() * sizeof(int),
                          XCL_BO_FLAGS_CACHEABLE, kernel.group_id(1));
  auto bo_inA = xrt::bo(device, IN_SIZE * sizeof(IN_DATATYPE),
                        XRT_BO_FLAGS_HOST_ONLY, kernel.group_id(3));
  auto bo_inB = xrt::bo(device, IN_SIZE * sizeof(IN_DATATYPE),
                        XRT_BO_FLAGS_HOST_ONLY, kernel.group_id(4));
  auto bo_out = xrt::bo(device, OUT_SIZE * sizeof(OUT_DATATYPE),
                        XRT_BO_FLAGS_HOST_ONLY, kernel.group_id(5));

  IN_DATATYPE *bufInA = bo_inA.map<IN_DATATYPE *>();
  std::vector<IN_DATATYPE> srcVecA;
  for (int i = 0; i < IN_SIZE; i++)
    srcVecA.push_back(i + 1);
  memcpy(bufInA, srcVecA.data(), (srcVecA.size() * sizeof(IN_DATATYPE)));

  void *bufInstr = bo_instr.map<void *>();
  memcpy(bufInstr, instr_v.data(), instr_v.size() * sizeof(int));

  bo_instr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  bo_inA.sync(XCL_BO_SYNC_BO_TO_DEVICE);

  unsigned int opcode = 3;
  auto run = kernel(opcode, bo_instr, instr_v.size(), bo_inA, bo_inB, bo_out);
  run.wait();

  bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

  OUT_DATATYPE *bufOut = bo_out.map<OUT_DATATYPE *>();

  int errors = 0;

  int idx = 0;
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 16; j++) {
      uint32_t ref = idx + 1 + 21;
      // Zero padding at first and last element for every 14 inputs.
      if ((j < 4) || (j >= 12))
        ref = 0 + 21;
      else
        idx++;
      if (*(bufOut + i * 16 + j) != ref) {
        std::cout << "Error in output " << std::to_string(bufOut[i * 16 + j])
                  << " != " << ref << std::endl;
        errors++;
      } else
        std::cout << "Correct output " << std::to_string(bufOut[i * 16 + j])
                  << " == " << ref << std::endl;
    }
  }

  if (!errors) {
    std::cout << "\nPASS!\n\n";
    return 0;
  }

  std::cout << "\nfailed.\n\n";
  return 1;
}
