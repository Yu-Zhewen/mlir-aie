// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (C) 2023, Advanced Micro Devices, Inc.

// REQUIRES: valid_xchess_license
// RUN: mkdir -p %t/data; cd %t
// RUN: aie-opt %s -affine-super-vectorize="virtual-vector-size=32" --convert-vector-to-aievec="aie-target=aieml" -lower-affine | aie-translate -aieml=true --aievec-to-cpp -o dut.cc
// RUN: xchesscc_wrapper %xchesscc_aie2_args +w work +o work -I%S -I. -c dut.cc -o dut.o
// RUN: xchesscc_wrapper %xchesscc_aie2_args +w work +o work -I%S -I. %S/testbench.cc work/dut.o
// RUN: xca_udm_dbg --aiearch aie-ml -qf -T -P %aietools/data/aie_ml/lib/ -t "%S/../profiling.tcl ./work/a.out" >& xca_udm_dbg.stdout
// RUN: FileCheck --input-file=./xca_udm_dbg.stdout %s
// CHECK: TEST PASSED

module {
  func.func @dut(%arg0: memref<1024xi8>, %arg1: memref<1024xi8>, %arg2: memref<1024xi32>) {
    affine.for %arg3 = 0 to 1024 {
      %0 = affine.load %arg0[%arg3] : memref<1024xi8>
      %1 = affine.load %arg1[%arg3] : memref<1024xi8>
      %2 = arith.extsi %0 : i8 to i32
      %3 = arith.extsi %1 : i8 to i32
      %4 = arith.muli %2, %3 : i32
      affine.store %4, %arg2[%arg3] : memref<1024xi32>
    }
    return
  }
}
