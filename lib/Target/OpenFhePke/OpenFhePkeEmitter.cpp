#include "lib/Target/OpenFhePke/OpenFhePkeEmitter.h"

#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "lib/Analysis/SelectVariableNames/SelectVariableNames.h"
#include "lib/Dialect/LWE/IR/LWEOps.h"
#include "lib/Dialect/Openfhe/IR/OpenfheOps.h"
#include "lib/Target/OpenFhePke/OpenFheUtils.h"
#include "lib/Utils/TargetUtils/TargetUtils.h"
#include "llvm/include/llvm/ADT/STLExtras.h"             // from @llvm-project
#include "llvm/include/llvm/ADT/SmallVector.h"           // from @llvm-project
#include "llvm/include/llvm/ADT/StringExtras.h"          // from @llvm-project
#include "llvm/include/llvm/ADT/TypeSwitch.h"            // from @llvm-project
#include "llvm/include/llvm/Support/FormatVariadic.h"    // from @llvm-project
#include "llvm/include/llvm/Support/raw_ostream.h"       // from @llvm-project
#include "mlir/include/mlir/Dialect/Arith/IR/Arith.h"    // from @llvm-project
#include "mlir/include/mlir/Dialect/Func/IR/FuncOps.h"   // from @llvm-project
#include "mlir/include/mlir/Dialect/Tensor/IR/Tensor.h"  // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinAttributes.h"      // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinOps.h"             // from @llvm-project
#include "mlir/include/mlir/IR/Diagnostics.h"            // from @llvm-project
#include "mlir/include/mlir/IR/Types.h"                  // from @llvm-project
#include "mlir/include/mlir/IR/Value.h"                  // from @llvm-project
#include "mlir/include/mlir/IR/ValueRange.h"             // from @llvm-project
#include "mlir/include/mlir/IR/Visitors.h"               // from @llvm-project
#include "mlir/include/mlir/Support/LLVM.h"              // from @llvm-project
#include "mlir/include/mlir/Support/LogicalResult.h"     // from @llvm-project

namespace mlir {
namespace heir {
namespace openfhe {

namespace {

FailureOr<std::string> printFloatAttr(FloatAttr floatAttr) {
  if (!floatAttr.getType().isF32() || !floatAttr.getType().isF64()) {
    return failure();
  }

  SmallString<128> strValue;
  auto apValue = APFloat(floatAttr.getValueAsDouble());
  apValue.toString(strValue, /*FormatPrecision=*/0, /*FormatMaxPadding=*/15,
                   /*TruncateZero=*/true);
  return std::string(strValue);
}

FailureOr<std::string> getStringForConstant(Value value) {
  if (auto constantOp =
          dyn_cast_or_null<arith::ConstantOp>(value.getDefiningOp())) {
    auto valueAttr = constantOp.getValue();
    if (auto intAttr = dyn_cast<IntegerAttr>(valueAttr)) {
      return std::to_string(intAttr.getInt());
    } else if (auto floatAttr = dyn_cast<FloatAttr>(valueAttr)) {
      return printFloatAttr(floatAttr);
    }
  }
  return failure();
}

}  // namespace

LogicalResult translateToOpenFhePke(Operation *op, llvm::raw_ostream &os,
                                    const OpenfheScheme &scheme) {
  SelectVariableNames variableNames(op);
  OpenFhePkeEmitter emitter(os, &variableNames, scheme);
  LogicalResult result = emitter.translate(*op);
  return result;
}

LogicalResult OpenFhePkeEmitter::translate(Operation &op) {
  LogicalResult status =
      llvm::TypeSwitch<Operation &, LogicalResult>(op)
          // Builtin ops
          .Case<ModuleOp>([&](auto op) { return printOperation(op); })
          // Func ops
          .Case<func::FuncOp, func::ReturnOp>(
              [&](auto op) { return printOperation(op); })
          // Arith ops
          .Case<arith::ConstantOp, arith::ExtSIOp, arith::IndexCastOp,
                arith::ExtFOp>([&](auto op) { return printOperation(op); })
          // Tensor ops
          .Case<tensor::EmptyOp, tensor::InsertOp, tensor::ExtractOp,
                tensor::SplatOp>([&](auto op) { return printOperation(op); })
          // LWE ops
          .Case<lwe::RLWEDecodeOp, lwe::ReinterpretUnderlyingTypeOp>(
              [&](auto op) { return printOperation(op); })
          // OpenFHE ops
          .Case<AddOp, AddPlainOp, SubOp, MulNoRelinOp, MulOp, MulPlainOp,
                SquareOp, NegateOp, MulConstOp, RelinOp, ModReduceOp,
                LevelReduceOp, RotOp, AutomorphOp, KeySwitchOp, EncryptOp,
                DecryptOp, GenParamsOp, GenContextOp, GenMulKeyOp, GenRotKeyOp,
                MakePackedPlaintextOp, MakeCKKSPackedPlaintextOp>(
              [&](auto op) { return printOperation(op); })
          .Default([&](Operation &) {
            return emitError(op.getLoc(), "unable to find printer for op");
          });

  if (failed(status)) {
    return emitError(op.getLoc(),
                     llvm::formatv("Failed to translate op {0}", op.getName()));
  }
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(ModuleOp moduleOp) {
  os << getModulePrelude(scheme_) << "\n";
  for (Operation &op : moduleOp) {
    if (failed(translate(op))) {
      return failure();
    }
  }

  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(func::FuncOp funcOp) {
  if (funcOp.getNumResults() != 1) {
    return emitError(funcOp.getLoc(),
                     llvm::formatv("Only functions with a single return type "
                                   "are supported, but this function has ",
                                   funcOp.getNumResults()));
    return failure();
  }

  Type result = funcOp.getResultTypes()[0];
  if (failed(emitType(result))) {
    return emitError(funcOp.getLoc(),
                     llvm::formatv("Failed to emit type {0}", result));
  }

  os << " " << funcOp.getName() << "(";
  os.indent();

  // Check the types without printing to enable failure outside of
  // commaSeparatedValues; maybe consider making commaSeparatedValues combine
  // the results into a FailureOr, like commaSeparatedTypes in tfhe_rust
  // emitter.
  for (Value arg : funcOp.getArguments()) {
    if (failed(convertType(arg.getType()))) {
      return emitError(funcOp.getLoc(),
                       llvm::formatv("Failed to emit type {0}", arg.getType()));
    }
  }

  os << commaSeparatedValues(funcOp.getArguments(), [&](Value value) {
    return convertType(value.getType()).value() + " " +
           variableNames->getNameForValue(value);
  });
  os.unindent();
  os << ") {\n";
  os.indent();

  for (Block &block : funcOp.getBlocks()) {
    for (Operation &op : block.getOperations()) {
      if (failed(translate(op))) {
        return failure();
      }
    }
  }

  os.unindent();
  os << "}\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(func::ReturnOp op) {
  if (op.getNumOperands() != 1) {
    return emitError(op.getLoc(), "Only one return value supported");
  }
  os << "return " << variableNames->getNameForValue(op.getOperands()[0])
     << ";\n";
  return success();
}

void OpenFhePkeEmitter::emitAutoAssignPrefix(Value result) {
  // Use const auto& because most OpenFHE API methods would perform a copy
  // if using a plain `auto`.
  os << "const auto& " << variableNames->getNameForValue(result) << " = ";
}

LogicalResult OpenFhePkeEmitter::emitTypedAssignPrefix(Value result) {
  if (failed(emitType(result.getType()))) {
    return failure();
  }
  os << " " << variableNames->getNameForValue(result) << " = ";
  return success();
}

LogicalResult OpenFhePkeEmitter::printEvalMethod(
    ::mlir::Value result, ::mlir::Value cryptoContext,
    ::mlir::ValueRange nonEvalOperands, std::string_view op) {
  emitAutoAssignPrefix(result);

  os << variableNames->getNameForValue(cryptoContext) << "->" << op << "(";
  os << commaSeparatedValues(nonEvalOperands, [&](Value value) {
    return variableNames->getNameForValue(value);
  });
  os << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(AddOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getLhs(), op.getRhs()}, "EvalAdd");
}

LogicalResult OpenFhePkeEmitter::printOperation(AddPlainOp op) {
  // OpenFHE defines an overload for EvalAdd to work on both plaintext and
  // ciphertext inputs.
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext(), op.getPlaintext()}, "EvalAdd");
}

LogicalResult OpenFhePkeEmitter::printOperation(SubOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getLhs(), op.getRhs()}, "EvalSub");
}

LogicalResult OpenFhePkeEmitter::printOperation(MulNoRelinOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getLhs(), op.getRhs()}, "EvalMultNoRelin");
}

LogicalResult OpenFhePkeEmitter::printOperation(MulOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getLhs(), op.getRhs()}, "EvalMult");
}

LogicalResult OpenFhePkeEmitter::printOperation(MulPlainOp op) {
  // OpenFHE defines an overload for EvalMult to work on both plaintext and
  // ciphertext inputs.
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext(), op.getPlaintext()}, "EvalMult");
}

LogicalResult OpenFhePkeEmitter::printOperation(MulConstOp op) {
  // OpenFHE defines an overload for EvalMult to work on constant inputs,
  // but only for some schemes.
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext(), op.getConstant()}, "EvalMult");
}

LogicalResult OpenFhePkeEmitter::printOperation(NegateOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext()}, "EvalNegate");
}

LogicalResult OpenFhePkeEmitter::printOperation(SquareOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext()}, "EvalSquare");
}

LogicalResult OpenFhePkeEmitter::printOperation(RelinOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext()}, "Relinearize");
}

LogicalResult OpenFhePkeEmitter::printOperation(ModReduceOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext()}, "ModReduce");
}

LogicalResult OpenFhePkeEmitter::printOperation(LevelReduceOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext()}, "LevelReduce");
}

LogicalResult OpenFhePkeEmitter::printOperation(RotOp op) {
  emitAutoAssignPrefix(op.getResult());

  os << variableNames->getNameForValue(op.getCryptoContext()) << "->"
     << "EvalRotate" << "("
     << variableNames->getNameForValue(op.getCiphertext()) << ", "
     << op.getIndex().getValue() << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(AutomorphOp op) {
  // EvalAutomorphism has a bit of a strange function signature in OpenFHE:
  //
  //     EvalAutomorphism(
  //       ConstCiphertext<DCRTPoly> ciphertext,
  //       int32_t i,
  //       const std::map<int32_t, EvalKey<DCRTPoly>>& evalKeyMap
  //     )
  //
  // Here i is an index to evalKeyMap, but no other data from evalKeyMap is
  // used. To match the API, we emit code that just creates a single-entry map
  // locally before calling EvalAutomorphism.
  //
  // This would probably be an easy upstream fix to add a specialized function
  // call if it becomes necessary.
  std::string mapName =
      variableNames->getNameForValue(op.getResult()) + "evalkeymap";
  auto result = convertType(op.getEvalKey().getType());
  os << "std::map<uint32_t, " << result << "> " << mapName << " = {{0, "
     << variableNames->getNameForValue(op.getEvalKey()) << "}};\n";

  emitAutoAssignPrefix(op.getResult());
  os << variableNames->getNameForValue(op.getCryptoContext())
     << "->EvalAutomorphism(";
  os << variableNames->getNameForValue(op.getCiphertext()) << ", 0, " << mapName
     << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(KeySwitchOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getCiphertext(), op.getEvalKey()}, "KeySwitch");
}

LogicalResult OpenFhePkeEmitter::printOperation(arith::ConstantOp op) {
  auto valueAttr = op.getValue();
  if (auto intAttr = dyn_cast<IntegerAttr>(valueAttr)) {
    // Constant integers may be unused if their uses directly output the
    // constant value (e.g. tensor.insert and tensor.extract use the defining
    // constant values of indices if available).
    os << "[[maybe_unused]] ";
    if (failed(emitTypedAssignPrefix(op.getResult()))) {
      return failure();
    }
    os << intAttr.getValue() << ";\n";
  } else if (auto floatAttr = dyn_cast<FloatAttr>(valueAttr)) {
    if (failed(emitTypedAssignPrefix(op.getResult()))) {
      return failure();
    }
    auto floatStr = printFloatAttr(floatAttr);
    if (failed(floatStr)) {
      return failure();
    }
    os << floatStr.value() << ";\n";
  } else if (auto denseElementsAttr = dyn_cast<DenseElementsAttr>(valueAttr)) {
    auto nonUnitDims = llvm::to_vector(
        llvm::make_filter_range(denseElementsAttr.getType().getShape(),
                                [](int dim) { return dim != 1; }));
    bool printMultiDimAsOneDim = nonUnitDims.size() == 1;
    if (denseElementsAttr.getType().getRank() == 1 || printMultiDimAsOneDim) {
      // Print a 1-D constant.
      // TODO(#913): This is a simplifying assumption on the layout of the
      // multi-dimensional when there is only one non-unit dimension.
      if (printMultiDimAsOneDim) {
        os << "std::vector<";
        if (failed(emitType(denseElementsAttr.getType().getElementType()))) {
          return failure();
        }
        os << ">";
      } else if (failed(emitType(op.getResult().getType()))) {
        return failure();
      }
      os << " " << variableNames->getNameForValue(op.getResult());

      std::string value_str;
      llvm::raw_string_ostream ss(value_str);
      denseElementsAttr.print(ss);

      if (denseElementsAttr.isSplat()) {
        // SplatElementsAttr are printed as dense<2> : tensor<1xi32>.
        // Output as `std::vector<int32_t> constant(2, 1);`
        int start = value_str.find('<') + 1;
        int end = value_str.find('>') - start;
        os << "(" << denseElementsAttr.getNumElements() << ", "
           << value_str.substr(start, end) << ");\n";
      } else {
        // DenseElementsAttr are printed as dense<[1, 2]> : tensor<2xi32>.
        // Output as `std::vector<int32_t> constant = {1, 2};`
        int start = value_str.find_last_of('[') + 1;
        int end = value_str.find_first_of(']') - start;
        os << " = {" << value_str.substr(start, end) << "};\n";
      }
      return success();
    }
    return failure();
  } else {
    return op.emitError() << "Unsupported constant type "
                          << valueAttr.getType();
  }
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(arith::ExtSIOp op) {
  // OpenFHE has a convention that all inputs to MakePackedPlaintext are
  // std::vector<int64_t>, so earlier stages in the pipeline emit typecasts

  std::string inputVarName = variableNames->getNameForValue(op.getOperand());
  std::string resultVarName = variableNames->getNameForValue(op.getResult());

  // If it's a vector<int**_t>, we can use a copy constructor to upcast.
  if (auto tensorTy = dyn_cast<RankedTensorType>(op.getOperand().getType())) {
    os << "std::vector<int64_t> " << resultVarName << "(std::begin("
       << inputVarName << "), std::end(" << inputVarName << "));\n";
  } else {
    return op.emitOpError() << "Unsupported input type";
  }

  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(arith::ExtFOp op) {
  // OpenFHE has a convention that all inputs to MakeCKKSPackedPlaintext are
  // std::vector<double>, so earlier stages in the pipeline emit typecasts

  std::string inputVarName = variableNames->getNameForValue(op.getOperand());
  std::string resultVarName = variableNames->getNameForValue(op.getResult());

  // If it's a vector<float>, we can use a copy constructor to upcast.
  if (auto tensorTy = dyn_cast<RankedTensorType>(op.getOperand().getType())) {
    os << "std::vector<double> " << resultVarName << "(std::begin("
       << inputVarName << "), std::end(" << inputVarName << "));\n";
  } else {
    return op.emitOpError() << "Unsupported input type";
  }

  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(arith::IndexCastOp op) {
  Type outputType = op.getOut().getType();
  if (failed(emitTypedAssignPrefix(op.getResult()))) {
    return failure();
  }
  os << "static_cast<";
  if (failed(emitType(outputType))) {
    return op.emitOpError() << "Unsupported index_cast op";
  }
  os << ">(" << variableNames->getNameForValue(op.getIn()) << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(tensor::EmptyOp op) {
  // std::vector<std::vector<CiphertextT>> result(dim0,
  // std::vector<CiphertextT>(dim1)); initStr = (dim1) initStr = (dim0,
  // std::vector<CiphertextT>{initStr})
  RankedTensorType resultTy = op.getResult().getType();
  auto elementTy = convertType(resultTy.getElementType());
  if (failed(elementTy)) {
    return failure();
  }
  if (failed(emitType(resultTy))) {
    return failure();
  }
  os << " " << variableNames->getNameForValue(op.getResult());
  std::string initStr = llvm::formatv("({0})", resultTy.getShape().back());
  for (auto dim :
       llvm::reverse(op.getResult().getType().getShape().drop_back(1))) {
    initStr = llvm::formatv("({0}, std::vector<{1}>{2})", dim,
                            elementTy.value(), initStr);
  }
  os << initStr << ";\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(tensor::ExtractOp op) {
  // const auto& v1 = in[0, 1];
  emitAutoAssignPrefix(op.getResult());
  os << variableNames->getNameForValue(op.getTensor());
  os << bracketEnclosedValues(op.getIndices(), [&](Value value) {
    auto constantStr = getStringForConstant(value);
    return constantStr.value_or(variableNames->getNameForValue(value));
  });
  os << ";\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(tensor::InsertOp op) {
  // For a tensor.insert MLIR statement, we assign the destination vector and
  // then move the vector to the result.
  //   %result = tensor.insert %scalar into %dest[%idx]
  // dest[idx] = scalar;
  // Type result = std::move(dest);
  os << variableNames->getNameForValue(op.getDest());
  os << bracketEnclosedValues(op.getIndices(), [&](Value value) {
    auto constantStr = getStringForConstant(value);
    return constantStr.value_or(variableNames->getNameForValue(value));
  });
  os << " = " << variableNames->getNameForValue(op.getScalar()) << ";\n";
  if (failed(emitTypedAssignPrefix(op.getResult()))) {
    return failure();
  }
  os << "std::move(" << variableNames->getNameForValue(op.getDest()) << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(tensor::SplatOp op) {
  // std::vector<CiphertextType> result(num, value);
  auto result = op.getResult();
  if (failed(emitType(result.getType()))) {
    return failure();
  }
  if (result.getType().getRank() != 1) {
    return failure();
  }
  os << " " << variableNames->getNameForValue(result) << "("
     << result.getType().getNumElements() << ", "
     << variableNames->getNameForValue(op.getInput()) << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(
    lwe::ReinterpretUnderlyingTypeOp op) {
  emitAutoAssignPrefix(op.getResult());
  os << variableNames->getNameForValue(op.getInput()) << ";\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(
    openfhe::MakePackedPlaintextOp op) {
  std::string inputVarName = variableNames->getNameForValue(op.getValue());

  emitAutoAssignPrefix(op.getResult());
  FailureOr<Value> resultCC = getContextualCryptoContext(op.getOperation());
  if (failed(resultCC)) return resultCC;
  os << variableNames->getNameForValue(resultCC.value())
     << "->MakePackedPlaintext(" << inputVarName << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(
    openfhe::MakeCKKSPackedPlaintextOp op) {
  if (scheme_ != OpenfheScheme::CKKS) {
    return emitError(op.getLoc(),
                     "encoding CKKS plaintext not supported by chosen scheme");
  }

  std::string inputVarName = variableNames->getNameForValue(op.getValue());

  emitAutoAssignPrefix(op.getResult());
  FailureOr<Value> resultCC = getContextualCryptoContext(op.getOperation());
  if (failed(resultCC)) return resultCC;
  os << variableNames->getNameForValue(resultCC.value())
     << "->MakeCKKSPackedPlaintext(" << inputVarName << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(lwe::RLWEDecodeOp op) {
  // In OpenFHE a plaintext is already decoded by decrypt. The internal OpenFHE
  // implementation is simple enough (and dependent on currently-hard-coded
  // encoding choices) that we will eventually need to work at a lower level of
  // the API to support this operation properly.
  auto tensorTy = dyn_cast<RankedTensorType>(op.getResult().getType());
  if (tensorTy) {
    if (tensorTy.getRank() != 1) {
      return emitError(op.getLoc(), "Only 1D tensors supported");
    }
    // OpenFHE plaintexts must be manually resized to the decoded output size
    // via plaintext->SetLength(<size>);
    auto size = tensorTy.getShape()[0];
    auto inputVarName = variableNames->getNameForValue(op.getInput());
    os << inputVarName << "->SetLength(" << size << ");\n";

    std::string tmpVar =
        variableNames->getNameForValue(op.getResult()) + "_cast";
    os << "const auto& " << tmpVar << " = ";
    os << inputVarName << "->GetPackedValue();\n";

    auto outputVarName = variableNames->getNameForValue(op.getResult());
    if (failed(emitType(tensorTy))) {
      return failure();
    }
    os << " " << outputVarName << "(std::begin(" << tmpVar << "), std::end("
       << tmpVar << "));\n";

    return success();
  }

  // By convention, a plaintext stores a scalar value in index 0
  auto result = emitTypedAssignPrefix(op.getResult());
  if (failed(result)) return result;
  os << variableNames->getNameForValue(op.getInput())
     << "->GetPackedValue()[0];\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(EncryptOp op) {
  return printEvalMethod(op.getResult(), op.getCryptoContext(),
                         {op.getPublicKey(), op.getPlaintext()}, "Encrypt");
}

LogicalResult OpenFhePkeEmitter::printOperation(DecryptOp op) {
  // Decrypt asks for a pointer to an outparam for the output plaintext
  os << "PlaintextT " << variableNames->getNameForValue(op.getResult())
     << ";\n";

  os << variableNames->getNameForValue(op.getCryptoContext()) << "->Decrypt(";
  os << commaSeparatedValues(
      {op.getPrivateKey(), op.getCiphertext()},
      [&](Value value) { return variableNames->getNameForValue(value); });
  os << ", &" << variableNames->getNameForValue(op.getResult()) << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(GenParamsOp op) {
  auto paramsName = variableNames->getNameForValue(op.getResult());
  int64_t mulDepth = op.getMulDepthAttr().getValue().getSExtValue();
  int64_t plainMod = op.getPlainModAttr().getValue().getSExtValue();

  os << "CCParamsT " << paramsName << ";\n";
  os << paramsName << ".SetMultiplicativeDepth(" << mulDepth << ");\n";
  os << paramsName << ".SetPlaintextModulus(" << plainMod << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(GenContextOp op) {
  auto paramsName = variableNames->getNameForValue(op.getParams());
  auto contextName = variableNames->getNameForValue(op.getResult());

  os << "CryptoContextT " << contextName << " = GenCryptoContext(" << paramsName
     << ");\n";
  os << contextName << "->Enable(PKE);\n";
  os << contextName << "->Enable(KEYSWITCH);\n";
  os << contextName << "->Enable(LEVELEDSHE);\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(GenMulKeyOp op) {
  auto contextName = variableNames->getNameForValue(op.getCryptoContext());
  auto privateKeyName = variableNames->getNameForValue(op.getPrivateKey());
  os << contextName << "->EvalMultKeyGen(" << privateKeyName << ");\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::printOperation(GenRotKeyOp op) {
  auto contextName = variableNames->getNameForValue(op.getCryptoContext());
  auto privateKeyName = variableNames->getNameForValue(op.getPrivateKey());

  std::vector<std::string> rotIndices;
  llvm::transform(op.getIndices(), std::back_inserter(rotIndices),
                  [](int64_t value) { return std::to_string(value); });

  os << contextName << "->EvalRotateKeyGen(" << privateKeyName << ", {";
  os << llvm::join(rotIndices, ", ");
  os << "});\n";
  return success();
}

LogicalResult OpenFhePkeEmitter::emitType(Type type) {
  auto result = convertType(type);
  if (failed(result)) {
    return failure();
  }
  os << result;
  return success();
}

OpenFhePkeEmitter::OpenFhePkeEmitter(raw_ostream &os,
                                     SelectVariableNames *variableNames,
                                     const OpenfheScheme &scheme)
    : scheme_(scheme), os(os), variableNames(variableNames) {}
}  // namespace openfhe
}  // namespace heir
}  // namespace mlir
