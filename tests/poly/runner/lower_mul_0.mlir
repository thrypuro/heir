// WARNING: this file is autogenerated. Do not edit manually, instead see
// tests/poly/runner/generate_test_cases.py

//-------------------------------------------------------
// entry and check_prefix are re-set per test execution
// DEFINE: %{entry} =
// DEFINE: %{check_prefix} =

// DEFINE: %{compile} = heir-opt %s --heir-polynomial-to-llvm
// DEFINE: %{run} = mlir-cpu-runner -e %{entry} -entry-point-result=void --shared-libs="%mlir_lib_dir/libmlir_c_runner_utils%shlibext,%mlir_runner_utils"
// DEFINE: %{check} = FileCheck %s --check-prefix=%{check_prefix}
//-------------------------------------------------------

func.func private @printMemrefI32(memref<*xi32>) attributes { llvm.emit_c_interface }

// REDEFINE: %{entry} = test_0
// REDEFINE: %{check_prefix} = CHECK_TEST_0
// RUN: %{compile} | %{run} | %{check}

#ideal_0 = #poly.polynomial<1 + x**12>
#ring_0 = #poly.ring<cmod=4294967296, ideal=#ideal_0>
!poly_ty_0 = !poly.poly<#ring_0>

func.func @test_0() {
  %const0 = arith.constant 0 : index
  %0 = poly.constant <1 + x**10> : !poly_ty_0
  %1 = poly.constant <1 + x**11> : !poly_ty_0
  %2 = poly.mul(%0, %1) : !poly_ty_0


  %tensor = poly.to_tensor %2 : !poly_ty_0 -> tensor<12xi32>

  %ref = bufferization.to_memref %tensor : memref<12xi32>
  %U = memref.cast %ref : memref<12xi32> to memref<*xi32>
  func.call @printMemrefI32(%U) : (memref<*xi32>) -> ()
  return
}
// expected_result: Poly(x**11 + x**10 - x**9 + 1, x, domain='ZZ[4294967296]')
// CHECK_TEST_0: [1, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1, 1]
