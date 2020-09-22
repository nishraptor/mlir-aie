// NOTE: Assertions have been autogenerated by utils/update_mlir_test_checks.py
// RUN: aie-opt --aie-llvm-lowering %s | FileCheck %s

// Test LLVM lowering for lock accesses and memory accesses (LockOp, UseLockOp, and BufferOp)
// Things to make sure:
//   - LockID: depending on which tile (or memory module) a lock is instantiated, create a lock ID
//             that has correct offset from a core's view (based on cardinal direction)
//   - Buffer: depending on which tile (or memory module) a buffer is instantiated, create an LLVM
//             static allocation (for now) for each core that can access to the buffer
module @test_core_llvm1 {
// CHECK:       module @test_core_llvm1 {
// CHECK:         llvm.mlir.global external @a() : !llvm.array<256 x i32>
// CHECK:         llvm.func @core11() {
// CHECK:           %[[VAL_0:.*]] = llvm.mlir.constant(256 : index) : !llvm.i64
// CHECK:           %[[VAL_1:.*]] = llvm.mlir.addressof @a : !llvm.ptr<array<256 x i32>>
// CHECK:           %[[VAL_2:.*]] = llvm.mlir.constant(0 : i32) : !llvm.i32
// CHECK:           %[[VAL_3:.*]] = llvm.mlir.constant(56 : i32) : !llvm.i32
// CHECK:           llvm.call @llvm.aie.lock.acquire.reg(%[[VAL_3]], %[[VAL_2]]) : (!llvm.i32, !llvm.i32) -> ()
// CHECK:           %[[VAL_4:.*]] = llvm.mlir.constant(1 : i32) : !llvm.i32
// CHECK:           %[[VAL_5:.*]] = llvm.mlir.constant(16 : index) : !llvm.i64
// CHECK:           %[[VAL_6:.*]] = llvm.mlir.constant(0 : index) : !llvm.i64
// CHECK:           %[[VAL_7:.*]] = llvm.getelementptr %[[VAL_1]]{{\[}}%[[VAL_6]], %[[VAL_5]]] : (!llvm.ptr<array<256 x i32>>, !llvm.i64, !llvm.i64) -> !llvm.ptr<i32>
// CHECK:           llvm.store %[[VAL_4]], %[[VAL_7]] : !llvm.ptr<i32>
// CHECK:           %[[VAL_8:.*]] = llvm.mlir.constant(1 : i32) : !llvm.i32
// CHECK:           %[[VAL_9:.*]] = llvm.mlir.constant(56 : i32) : !llvm.i32
// CHECK:           llvm.call @llvm.aie.lock.release.reg(%[[VAL_9]], %[[VAL_8]]) : (!llvm.i32, !llvm.i32) -> ()
// CHECK:           llvm.return
// CHECK:         }
// CHECK:         llvm.func @core21() {
// CHECK:           %[[VAL_10:.*]] = llvm.mlir.constant(256 : index) : !llvm.i64
// CHECK:           %[[VAL_11:.*]] = llvm.mlir.addressof @a : !llvm.ptr<array<256 x i32>>
// CHECK:           %[[VAL_12:.*]] = llvm.mlir.constant(1 : i32) : !llvm.i32
// CHECK:           %[[VAL_13:.*]] = llvm.mlir.constant(24 : i32) : !llvm.i32
// CHECK:           llvm.call @llvm.aie.lock.acquire.reg(%[[VAL_13]], %[[VAL_12]]) : (!llvm.i32, !llvm.i32) -> ()
// CHECK:           %[[VAL_14:.*]] = llvm.mlir.constant(16 : index) : !llvm.i64
// CHECK:           %[[VAL_15:.*]] = llvm.mlir.constant(0 : index) : !llvm.i64
// CHECK:           %[[VAL_16:.*]] = llvm.getelementptr %[[VAL_11]]{{\[}}%[[VAL_15]], %[[VAL_14]]] : (!llvm.ptr<array<256 x i32>>, !llvm.i64, !llvm.i64) -> !llvm.ptr<i32>
// CHECK:           %[[VAL_17:.*]] = llvm.load %[[VAL_16]] : !llvm.ptr<i32>
// CHECK:           %[[VAL_18:.*]] = llvm.mlir.constant(0 : i32) : !llvm.i32
// CHECK:           %[[VAL_19:.*]] = llvm.mlir.constant(24 : i32) : !llvm.i32
// CHECK:           llvm.call @llvm.aie.lock.release.reg(%[[VAL_19]], %[[VAL_18]]) : (!llvm.i32, !llvm.i32) -> ()
// CHECK:           llvm.return
// CHECK:         }
// CHECK:       }
  %tile11 = AIE.tile(1, 1)
  %tile21 = AIE.tile(2, 1)

  %lock11_8 = AIE.lock(%tile11, 8)
  %buf11_0  = AIE.buffer(%tile11) { sym_name = "a" } : memref<256xi32>

  %core11 = AIE.core(%tile11) {
    AIE.useLock(%lock11_8, "Acquire", 0, 0)
    %0 = constant 1 : i32
    %i = constant 16 : index
    store %0, %buf11_0[%i] : memref<256xi32>
    AIE.useLock(%lock11_8, "Release", 1, 0)
    AIE.end
  }

  %core21 = AIE.core(%tile21) {
    AIE.useLock(%lock11_8, "Acquire", 1, 0)
    %i = constant 16 : index
    %0 = load %buf11_0[%i] : memref<256xi32>
    AIE.useLock(%lock11_8, "Release", 0, 0)
    AIE.end
  }
}
