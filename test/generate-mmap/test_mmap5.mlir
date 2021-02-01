// RUN: aie-translate --aie-generate-mmap %s | FileCheck %s

// CHECK-LABEL: Tile(3, 3)
// CHECK-NEXT: Memory map: name base_address num_bytes
// CHECK-NEXT: _symbol south 0x20000 16
// CHECK-NEXT: _symbol west 0x28000 16
// CHECK-NEXT: _symbol north 0x30000 16
// CHECK-NEXT: _symbol same 0x38000 16

module @test_mmap1 {
  %tsame = AIE.tile(3, 3)
  %twest = AIE.tile(2, 3) // Different column
  %teast = AIE.tile(4, 3) // Different column
  %tsouth = AIE.tile(3, 2) // Different row
  %tnorth = AIE.tile(3, 4) // Different row

  %bufsame = AIE.buffer(%tsame) { sym_name = "same", address = 0x0 } : memref<4xi32>
  %bufeast = AIE.buffer(%teast) { sym_name = "east", address = 0x0 } : memref<4xi32>
  %bufwest = AIE.buffer(%twest) { sym_name = "west", address = 0x0 } : memref<4xi32>
  %bufsouth = AIE.buffer(%tsouth) { sym_name = "south", address = 0x0 } : memref<4xi32>
  %bufnorth = AIE.buffer(%tnorth) { sym_name = "north", address = 0x0 } : memref<4xi32>
}
