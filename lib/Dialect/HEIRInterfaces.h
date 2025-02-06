#ifndef LIB_DIALECT_HEIRINTERFACES_H_
#define LIB_DIALECT_HEIRINTERFACES_H_

#include "mlir/include/mlir/IR/Builders.h"               // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinAttributes.h"      // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinTypes.h"           // from @llvm-project
#include "mlir/include/mlir/IR/Dialect.h"                // from @llvm-project
#include "mlir/include/mlir/IR/DialectImplementation.h"  // from @llvm-project

// Don't mess up order
#include "lib/Dialect/HEIRInterfaces.h.inc"

namespace mlir {
namespace heir {

void registerOperandAndResultAttrInterface(DialectRegistry &registry);

}  // namespace heir
}  // namespace mlir

#endif  // LIB_DIALECT_HEIRINTERFACES_H_
