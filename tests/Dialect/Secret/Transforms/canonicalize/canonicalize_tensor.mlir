// RUN: heir-opt --canonicalize %s | FileCheck %s

// Regression test for issue #954 to ensure that generics wrapping tensor.empty
// operations do not collapse.

module {
  // CHECK: @main
  func.func @main(%arg0: !secret.secret<tensor<28x28xf32>>, %arg1: !secret.secret<tensor<784x10xf32>>, %arg2: !secret.secret<tensor<1x10xf32>>) -> !secret.secret<tensor<1x10xf32>> {
    %c0 = arith.constant 0 : index
    %cst = arith.constant 0.000000e+00 : f32
    %0 = secret.generic ins(%arg0, %arg1, %arg2 : !secret.secret<tensor<28x28xf32>>, !secret.secret<tensor<784x10xf32>>, !secret.secret<tensor<1x10xf32>>) {
    ^bb0(%arg3: tensor<28x28xf32>, %arg4: tensor<784x10xf32>, %arg5: tensor<1x10xf32>):
      %s_1 = "tosa.const_shape"() {values = dense<[1, 1, 784]> : tensor<3xindex>} : () -> !tosa.shape<3>
      %1 = tosa.reshape %arg3, %s_1 : (tensor<28x28xf32>, !tosa.shape<3>) -> tensor<1x1x784xf32>
      %s_2 = "tosa.const_shape"() {values = dense<[1, 784, 10]> : tensor<3xindex>} : () -> !tosa.shape<3>
      %2 = tosa.reshape %arg4, %s_2 : (tensor<784x10xf32>, !tosa.shape<3>) -> tensor<1x784x10xf32>
      %3 = tensor.empty() : tensor<1x1x10xf32>
      %4 = affine.for %arg6 = 0 to 10 iter_args(%arg7 = %3) -> (tensor<1x1x10xf32>) {
        %inserted = tensor.insert %cst into %arg7[%c0, %c0, %arg6] : tensor<1x1x10xf32>
        affine.yield %inserted : tensor<1x1x10xf32>
      }
      %5 = affine.for %arg6 = 0 to 10 iter_args(%arg7 = %4) -> (tensor<1x1x10xf32>) {
        %9 = affine.for %arg8 = 0 to 784 iter_args(%arg9 = %arg7) -> (tensor<1x1x10xf32>) {
          %extracted = tensor.extract %1[%c0, %c0, %arg8] : tensor<1x1x784xf32>
          %extracted_0 = tensor.extract %2[%c0, %arg8, %arg6] : tensor<1x784x10xf32>
          %extracted_1 = tensor.extract %4[%c0, %c0, %arg6] : tensor<1x1x10xf32>
          %10 = arith.mulf %extracted, %extracted_0 : f32
          %11 = arith.addf %extracted_1, %10 : f32
          %inserted = tensor.insert %11 into %arg9[%c0, %c0, %arg6] : tensor<1x1x10xf32>
          affine.yield %inserted : tensor<1x1x10xf32>
        }
        affine.yield %9 : tensor<1x1x10xf32>
      }
      %s_6 = "tosa.const_shape"() {values = dense<[1, 10]> : tensor<2xindex>} : () -> !tosa.shape<2>
      %6 = tosa.reshape %5, %s_6 : (tensor<1x1x10xf32>, !tosa.shape<2>) -> tensor<1x10xf32>
      %7 = tensor.empty() : tensor<1x10xf32>
      %8 = affine.for %arg6 = 0 to 10 iter_args(%arg7 = %7) -> (tensor<1x10xf32>) {
        %extracted = tensor.extract %6[%c0, %arg6] : tensor<1x10xf32>
        %extracted_0 = tensor.extract %arg5[%c0, %arg6] : tensor<1x10xf32>
        %9 = arith.addf %extracted, %extracted_0 : f32
        %10 = arith.maximumf %9, %cst : f32
        %inserted = tensor.insert %10 into %arg7[%c0, %arg6] : tensor<1x10xf32>
        affine.yield %inserted : tensor<1x10xf32>
      }
      secret.yield %8 : tensor<1x10xf32>
    } -> !secret.secret<tensor<1x10xf32>>
    return %0 : !secret.secret<tensor<1x10xf32>>
  }
}
