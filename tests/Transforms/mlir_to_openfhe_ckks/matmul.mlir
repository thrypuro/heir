// RUN: heir-opt --mlir-print-local-scope --affine-loop-normalize='promote-single-iter=1' --mlir-to-ckks --scheme-to-openfhe %s | FileCheck %s

// This pipeline fully loop unrolls the matmul.

module {
  // CHECK-LABEL: func @main
  // CHECK-SAME: %[[arg0:.*]]: tensor<1x16x!lwe.new_lwe_ciphertext<{{.*}}message_type = f32{{.*}}>>, %[[arg1:.*]]: tensor<1x16x!lwe.new_lwe_ciphertext<{{.*}}message_type = f32{{.*}}>>
  func.func @main(%arg0: tensor<1x16xf32> {secret.secret}, %arg1: tensor<1x16xf32> {secret.secret}) -> tensor<1x16xf32> {
    // CHECK-NOT: secret
    // CHECK-COUNT-256: openfhe.mul_plain
    // CHECK-COUNT-16: openfhe.mod_reduce
    // CHECK: return
    // CHECK-SAME: tensor<1x16x!lwe.new_lwe_ciphertext<{{.*}}message_type = f32{{.*}}>>
    %0 = "tosa.const"() <{values = dense<"0x5036CB3DE147C3BEE4A9393E47C021BE40F376BFA1078D3E8D53EB3DD6E0493EEFFC3CBFBEB947BE4597B5BBD185903E9B9C1BBEEB0713BD23B418BF66736C3EABF141BEED693F3E584F72BF3CB9E83EBD0E8D3E4D87BDBE5A0439BFBE94AABECDCA91BE695FA93E870B93BE576920BF6294083F4C08633DCBACC6BDD8C9243F6CAA17BE63FE853E647E8E3F27116D3DBA00FA3DDDD4C93EA96AA03E1FD4A7BE3C3297BD387D02BFA695923E3402CB3E6A4E0F3E8D0700BF195E3E3ECA2E0EBF28F39CBC21AE853F26F7803F1C7029BFAA05383F0DBFF0BEBF82CB3F8D9F843D3640A63F75BF7FBEF615053D1937CB3FC68B41BEEC66B9BED998223F90944FBD511F9DBFE8A4C23F3C11793F68822ABFBEE1923F32109FBF79DF193F726237BFBF6FFB3F55B69FBF1EFEF83E7D4EB63F553A37BF50F054BF4072EE3FACE7A1BF79CC633C8E44723F8D844E3FBE51FABE7BFA0F3F83F258BEC956703F4E00073FE1645E3F9C8203BF6D8B66BD1936893F0042113E6EC745BF161EB23E570AFE3D961D7C3E1039C43D665C36BF2791AF3D47B452BE34128B3E77DDA1BFADE2DCBE29DA10BEA4569FBD24B92ABE4DF072BFAE8AA13E4C661B3F3DCF823E4FDC1F3EB562A1BD5FEE0ABD8FB21CBFCE54193FF31C79BE2A0A763EA3B655BFD7EDB93CEE8F443E1C9693BDB0863BBF7F70C5BEC166A93EAF6CACBDBF5F023FEC98153FC2D49C3FD9A115C01EC09EBF36CD1B3F9ABE19C05129963F4CA4BDBFAB2F1C3E309239C03EF9903F12360BBFD15A1FC0733F6C3F8D4BDF3F615C2DC083868D3F497814BD8523E03DCCA8D6BDEA77253E99D43DBEECACB53E74A657BF7AE739BECC272F3F842D833E90A07CBFEEFCFBBD97BE063E7CE7DA3EBF4AEABD473F593FE32D25BF911CAB3F07ED413DD65AFDBE532AA23F85E451BF88925B3E3D09BD3D6C0AC33F2B3A19BFB0C3163F7803133F051EBDBE94A451BF1F83C13F9FD976BFB809763EDF71D0BD4BC424BE13E9853ED757033F15A656BE522F40BEA19AA4BEF1F9953E0FDF2E3D198BD9BED1DB2ABFCDB2E83EAE500E3FE4AA0B3F0284113EF339193FCD4C10BF382D6CBE7A020C3F016DA2BE590DF63E1923163D8B94383D1AD4EABEFDA50DBBBDA0BABCA75C0DBF5D971B3FDC29103F598190BF0C8726BEA2AD41BE1B19E13CC88265BF2DD392BD6509A73ED73A6F3EC4280CBFC24284BE76727CBEC5DE023F79B7B8BE6E8E23BF12C739BD1091853ECC190A3F369C0D3F65AB74BF1A15F63EA1F5FA3E6B2BABBE9C4FECB895E499BE2D0268BE8EA7EABE374FFD3DADBB19BFC759C83F4A69D73E37B836BFE5F4E1BEED900CBEECD986BFAE4E853D022E55BE1CDB073F6E31C9BEC202C5BE4BF853BEEE54DB3EEBC9613E74C317BEB9F2A3BE755B6F3F37CE383FF01E2F3D989532BE1C591EBE19464BBF"> : tensor<16x16xf32>}> : () -> tensor<16x16xf32>
    %1 = affine.for %arg2 = 0 to 1 iter_args(%arg3 = %arg1) -> (tensor<1x16xf32>) {
      %2 = affine.for %arg4 = 0 to 16 iter_args(%arg5 = %arg3) -> (tensor<1x16xf32>) {
        %3 = affine.for %arg6 = 0 to 16 iter_args(%arg7 = %arg5) -> (tensor<1x16xf32>) {
          %extracted = tensor.extract %arg0[%arg2, %arg6] : tensor<1x16xf32>
          %extracted_0 = tensor.extract %0[%arg6, %arg4] : tensor<16x16xf32>
          %extracted_1 = tensor.extract %arg7[%arg2, %arg4] : tensor<1x16xf32>
          %4 = arith.mulf %extracted, %extracted_0 : f32
          %5 = arith.addf %extracted_1, %4 : f32
          %inserted = tensor.insert %5 into %arg7[%arg2, %arg4] : tensor<1x16xf32>
          affine.yield %inserted : tensor<1x16xf32>
        }
        affine.yield %3 : tensor<1x16xf32>
      }
      affine.yield %2 : tensor<1x16xf32>
    }
    return %1 : tensor<1x16xf32>
  }
}
