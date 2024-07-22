//===- broadcast.mlir ------------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-opt --aie-create-pathfinder-flows --aie-find-flows %s | FileCheck %s
// CHECK: %[[T03:.*]] = aie.tile(0, 3)
// CHECK: %[[T02:.*]] = aie.tile(0, 2)
// CHECK: %[[T00:.*]] = aie.tile(0, 0)
// CHECK: %[[T13:.*]] = aie.tile(1, 3)
// CHECK: %[[T11:.*]] = aie.tile(1, 1)
// CHECK: %[[T10:.*]] = aie.tile(1, 0)
// CHECK: %[[T20:.*]] = aie.tile(2, 0)
// CHECK: %[[T30:.*]] = aie.tile(3, 0)
// CHECK: %[[T22:.*]] = aie.tile(2, 2)
// CHECK: %[[T31:.*]] = aie.tile(3, 1)
// CHECK: %[[T60:.*]] = aie.tile(6, 0)
// CHECK: %[[T70:.*]] = aie.tile(7, 0)
// CHECK: %[[T71:.*]] = aie.tile(7, 1)
// CHECK: %[[T72:.*]] = aie.tile(7, 2)
// CHECK: %[[T73:.*]] = aie.tile(7, 3)
// CHECK: %[[T80:.*]] = aie.tile(8, 0)
// CHECK: %[[T82:.*]] = aie.tile(8, 2)
// CHECK: %[[T83:.*]] = aie.tile(8, 3)
//
// CHECK: aie.flow(%[[T20]], DMA : 0, %[[T82]], DMA : 0)
// CHECK: aie.flow(%[[T20]], DMA : 0, %[[T71]], DMA : 0)
// CHECK: aie.flow(%[[T20]], DMA : 0, %[[T31]], DMA : 0)
// CHECK: aie.flow(%[[T20]], DMA : 0, %[[T13]], DMA : 0)
// CHECK: aie.flow(%[[T60]], DMA : 0, %[[T83]], DMA : 1)
// CHECK: aie.flow(%[[T60]], DMA : 0, %[[T22]], DMA : 1)
// CHECK: aie.flow(%[[T60]], DMA : 0, %[[T31]], DMA : 1)
// CHECK: aie.flow(%[[T60]], DMA : 0, %[[T02]], DMA : 1)

module {
    aie.device(xcvc1902) {
        %t03 = aie.tile(0, 3)
        %t02 = aie.tile(0, 2)
        %t00 = aie.tile(0, 0)
        %t13 = aie.tile(1, 3)
        %t11 = aie.tile(1, 1)
        %t10 = aie.tile(1, 0)
        %t20 = aie.tile(2, 0)
        %t30 = aie.tile(3, 0)
        %t22 = aie.tile(2, 2)
        %t31 = aie.tile(3, 1)
        %t60 = aie.tile(6, 0)
        %t70 = aie.tile(7, 0)
        %t71 = aie.tile(7, 1)
        %t72 = aie.tile(7, 2)
        %t73 = aie.tile(7, 3)
        %t80 = aie.tile(8, 0)
        %t82 = aie.tile(8, 2)
        %t83 = aie.tile(8, 3)

        aie.flow(%t20, DMA : 0, %t13, DMA : 0)
        aie.flow(%t20, DMA : 0, %t31, DMA : 0)
        aie.flow(%t20, DMA : 0, %t71, DMA : 0)
        aie.flow(%t20, DMA : 0, %t82, DMA : 0)

        aie.flow(%t60, DMA : 0, %t02, DMA : 1)
        aie.flow(%t60, DMA : 0, %t83, DMA : 1)
        aie.flow(%t60, DMA : 0, %t22, DMA : 1)
        aie.flow(%t60, DMA : 0, %t31, DMA : 1)
    }
}

