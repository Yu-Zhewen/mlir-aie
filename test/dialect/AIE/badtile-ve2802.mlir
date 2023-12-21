//===- badtile2.mlir -------------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2023 Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

// RUN: not %PYTHON aiecc.py %s 2>&1 | FileCheck %s
// CHECK: error{{.*}}'aie.tile' op column index (50) must be less than the number of columns in the device (38)

module @test {
 aie.device(xcve2802) {
  %t1 = aie.tile(50, 50)
 }
}
