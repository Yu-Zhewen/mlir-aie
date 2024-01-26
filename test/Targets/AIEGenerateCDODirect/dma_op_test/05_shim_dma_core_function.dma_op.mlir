// (c) Copyright 2023 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// REQUIRES: cdo_direct_generation
//
// RUN: export BASENAME=$(basename -s .dma_op.mlir %s)
// RUN: rm -rf *.elf* *.xclbin *.bin $BASENAME.dma_op.prj $BASENAME.dma_start.prj

// RUN: mkdir $BASENAME.dma_start.prj && pushd $BASENAME.dma_start.prj && %python aiecc.py --no-compile-host --tmpdir $PWD %S/$BASENAME.dma_start && popd
// RUN: aie-translate --aie-generate-cdo $BASENAME.dma_start.prj/input_physical.mlir --work-dir-path=$BASENAME.dma_start.prj

// RUN: mkdir $BASENAME.dma_op.prj && pushd $BASENAME.dma_op.prj && %python aiecc.py --no-compile-host --tmpdir $PWD %s && popd
// RUN: aie-translate --aie-generate-cdo $BASENAME.dma_op.prj/input_physical.mlir --work-dir-path=$BASENAME.dma_op.prj

// RUN: cmp $BASENAME.dma_op.prj/aie_cdo_error_handling.bin $BASENAME.dma_start.prj/aie_cdo_error_handling.bin
// RUN: cmp $BASENAME.dma_op.prj/aie_cdo_init.bin $BASENAME.dma_start.prj/aie_cdo_init.bin
// RUN: cmp $BASENAME.dma_op.prj/aie_cdo_elfs.bin $BASENAME.dma_start.prj/aie_cdo_elfs.bin
// RUN: cmp $BASENAME.dma_op.prj/aie_cdo_enable.bin $BASENAME.dma_start.prj/aie_cdo_enable.bin

module @test_chess_05_shim_dma_core_function {
  aie.device(ipu) {
    %tile_0_3 = aie.tile(0, 3)
    %tile_0_2 = aie.tile(0, 2)
    %tile_0_1 = aie.tile(0, 1)
    %tile_0_0 = aie.tile(0, 0)
    %a_ping = aie.buffer(%tile_0_3) {sym_name = "a_ping"} : memref<16xi32>
    %a_pong = aie.buffer(%tile_0_3) {sym_name = "a_pong"} : memref<16xi32>
    %b_ping = aie.buffer(%tile_0_3) {sym_name = "b_ping"} : memref<16xi32>
    %b_pong = aie.buffer(%tile_0_3) {sym_name = "b_pong"} : memref<16xi32>
    %lock_0_3 = aie.lock(%tile_0_3, 3) {init = 2 : i32}
    %lock_0_3_0 = aie.lock(%tile_0_3, 4)
    %lock_0_3_1 = aie.lock(%tile_0_3, 5) {init = 2 : i32}
    %lock_0_3_2 = aie.lock(%tile_0_3, 6)
    %lock_0_3_3 = aie.lock(%tile_0_3, 7)
    // func.func private @func(memref<16xi32>, memref<16xi32>)
    %core_0_3 = aie.core(%tile_0_3) {
      %c0 = arith.constant 0 : index
      %c1 = arith.constant 1 : index
      %c1_4 = arith.constant 1 : index
      scf.for %arg0 = %c0 to %c1 step %c1_4 {
        aie.use_lock(%lock_0_3_0, AcquireGreaterEqual, 1)
        aie.use_lock(%lock_0_3_1, AcquireGreaterEqual, 1)
        // func.call @func(%a_ping, %b_ping) : (memref<16xi32>, memref<16xi32>) -> ()
        aie.use_lock(%lock_0_3, Release, 1)
        aie.use_lock(%lock_0_3_2, Release, 1)
        aie.use_lock(%lock_0_3_0, AcquireGreaterEqual, 1)
        aie.use_lock(%lock_0_3_1, AcquireGreaterEqual, 1)
        // func.call @func(%a_pong, %b_pong) : (memref<16xi32>, memref<16xi32>) -> ()
        aie.use_lock(%lock_0_3, Release, 1)
        aie.use_lock(%lock_0_3_2, Release, 1)
      }
      aie.end
    } // {link_with = "kernel.o"}
    %mem_0_3 = aie.mem(%tile_0_3) {
      aie.dma(S2MM, 0) [{
        aie.use_lock(%lock_0_3, AcquireGreaterEqual, 1)
        aie.dma_bd(%a_ping : memref<16xi32>, 0, 16)
        aie.use_lock(%lock_0_3_0, Release, 1)
      }, {
        aie.use_lock(%lock_0_3, AcquireGreaterEqual, 1)
        aie.dma_bd(%a_pong : memref<16xi32>, 0, 16)
        aie.use_lock(%lock_0_3_0, Release, 1)
      }]
      aie.dma(MM2S, 1) [{
        aie.use_lock(%lock_0_3_2, AcquireGreaterEqual, 1)
        aie.dma_bd(%b_ping : memref<16xi32>, 0, 16)
        aie.use_lock(%lock_0_3_1, Release, 1)
      }, {
        aie.use_lock(%lock_0_3_2, AcquireGreaterEqual, 1)
        aie.dma_bd(%b_pong : memref<16xi32>, 0, 16)
        aie.use_lock(%lock_0_3_1, Release, 1)
      }]
      aie.end
    }
  }
}

