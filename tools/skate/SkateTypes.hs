{-
  SkateTypes: All defined builtin types

  Part of Skate: a Schema specification languge

  Copyright (c) 2017, ETH Zurich.
  All rights reserved.

  This file is distributed under the terms in the attached LICENSE file.
  If you do not find this file, copies can be found by writing to:
  ETH Zurich D-INFK, Universit\"atstr. 6, CH-8092 Zurich. Attn: Systems Group.
-}


module SkateTypes where

import qualified CAbsSyntax as C

data TypeBuiltIn = UInt8 | UInt16 | UInt32 | UInt64 | UIntPtr
                  | Int8 | Int16  | Int32  | Int64  | IntPtr
                  | Size | Char   | Bool   | String | Capref
                    deriving (Enum, Eq)


{-types -}
data TypeRef = TEnum String String
             | TConstant String String
             | TFact String String
             | TBuiltIn TypeBuiltIn
             | TFlags String String
             deriving(Eq)

instance Show TypeRef where
    show (TEnum t _) = "TEnum(" ++ t ++ ")"
    show (TConstant t _) = "TConstant(" ++ t ++ ")"
    show (TFact t _) = "TFact(" ++ t ++ ")"
    show (TBuiltIn t) = "TBuiltIn(" ++ (show t) ++ ")"
    show (TFlags t _)  = "TFlags(" ++ t ++ ")"


{- -}
instance Show TypeBuiltIn where
    show UInt8   = "uint8"
    show UInt16  = "uint16"
    show UInt32  = "uint32"
    show UInt64  = "uint64"
    show UIntPtr = "uintptr"
    show Int8    = "int8"
    show Int16   = "int16"
    show Int32   = "int32"
    show Int64   = "int64"
    show IntPtr  = "intptr"
    show Size    = "size"
    show Bool    = "bool"
    show String  = "string"
    show Char    = "char"
    show Capref  = "capref"

typeref_to_ctype :: TypeRef -> C.TypeSpec
typeref_to_ctype (TBuiltIn UInt8)   = C.TypeName "uint8_t"
typeref_to_ctype (TBuiltIn UInt16)  = C.TypeName "uint16_t"
typeref_to_ctype (TBuiltIn UInt32)  = C.TypeName "uint32_t"
typeref_to_ctype (TBuiltIn UInt64)  = C.TypeName "uint64_t"
typeref_to_ctype (TBuiltIn UIntPtr) = C.TypeName "uintptr_t"
typeref_to_ctype (TBuiltIn Int8)    = C.TypeName "int8_t"
typeref_to_ctype (TBuiltIn Int16)   = C.TypeName "int16_t"
typeref_to_ctype (TBuiltIn Int32)   = C.TypeName "int32_t"
typeref_to_ctype (TBuiltIn Int64)   = C.TypeName "int64_t"
typeref_to_ctype (TBuiltIn IntPtr)  = C.TypeName "intptr_t"
typeref_to_ctype (TBuiltIn Size)    = C.TypeName "size_t"
typeref_to_ctype (TBuiltIn Bool)    = C.TypeName "bool"
typeref_to_ctype (TBuiltIn String)  = C.Ptr (C.TypeName "char")
typeref_to_ctype (TBuiltIn Char)    = C.TypeName "char"
typeref_to_ctype (TBuiltIn Capref)  = C.Struct "capref"
typeref_to_ctype (TEnum i _)        = C.TypeName "TODO: SkateTypes.hs" -- (make_type_name (identifier_to_cname i))
typeref_to_ctype (TConstant i _)    = C.TypeName "TODO: SkateTypes.hs" -- (make_type_name (identifier_to_cname i))typeref_to_ctype (TFact i _)        = C.TypeName "TODO: SkateTypes.hs" -- (make_type_name (identifier_to_cname i))
typeref_to_ctype (TFlags i _)       = C.TypeName "TODO: SkateTypes.hs" -- (make_type_name (identifier_to_cname i))



instance Read TypeBuiltIn where
    readsPrec _ = \s -> case s of
        "uint8" -> [(UInt8, "")]
        "uint16" -> [(UInt16, "")]
        "uint32" -> [(UInt32, "")]
        "uint64" -> [(UInt64, "")]
        "uintptr" -> [(UIntPtr, "")]
        "int8" -> [(Int8, "")]
        "int16" -> [(Int16, "")]
        "int32" -> [(Int32, "")]
        "int64" -> [(Int64, "")]
        "intptr" -> [(IntPtr, "")]
        "size" -> [(Size, "")]
        "bool" -> [(Bool, "")]
        "string" -> [(String, "")]
        "char" -> [(Char, "")]
        "capref" -> [(Capref, "")]
        _ -> error  $ "Undefined builtin type " ++ s

findBuiltIntType :: String -> TypeBuiltIn
findBuiltIntType "uint8" = UInt8
findBuiltIntType "uint16" = UInt16
findBuiltIntType "uint32" = UInt32
findBuiltIntType "uint64" = UInt64
findBuiltIntType "uintptr" = UIntPtr
findBuiltIntType "int8" = Int8
findBuiltIntType "int16" = Int16
findBuiltIntType "int32" = Int32
findBuiltIntType "int64" = Int64
findBuiltIntType "intptr" = IntPtr
findBuiltIntType "size" = Size
findBuiltIntType "bool" = Bool
findBuiltIntType "string" = String
findBuiltIntType "char" = Char
findBuiltIntType "capref" = Capref
findBuiltIntType s = error  $ "Undefined builtin type " ++ s




builtin_fmt_wr :: TypeBuiltIn -> String
builtin_fmt_wr (UInt8)   = "PRIu8"
builtin_fmt_wr (UInt16)  = "PRIu16"
builtin_fmt_wr (UInt32)  = "PRIu32"
builtin_fmt_wr (UInt64)  = "PRIu64"
builtin_fmt_wr (UIntPtr) = "PRIuPTR"
builtin_fmt_wr (Int8)    = "PRIi8"
builtin_fmt_wr (Int16)   = "PRIi16"
builtin_fmt_wr (Int32)   = "PRIi32"
builtin_fmt_wr (Int64)   = "PRIi64"
builtin_fmt_wr (IntPtr)  = "PRIuPTR"
builtin_fmt_wr (Size)    = "PRIuSIZE"
builtin_fmt_wr (Bool)    = "\"i\""
builtin_fmt_wr (String)  = "\"s\""
builtin_fmt_wr (Char)    = "\"c\""


builtin_fmt_rd :: TypeBuiltIn -> String
builtin_fmt_rd (UInt8)   = "SCNu8"
builtin_fmt_rd (UInt16)  = "SCNu16"
builtin_fmt_rd (UInt32)  = "SCNu32"
builtin_fmt_rd (UInt64)  = "SCNu64"
builtin_fmt_rd (UIntPtr) = "SCNuPTR"
builtin_fmt_rd (Int8)    = "SCNi8"
builtin_fmt_rd (Int16)   = "SCNi16"
builtin_fmt_rd (Int32)   = "SCNi32"
builtin_fmt_rd (Int64)   = "SCNi64"
builtin_fmt_rd (IntPtr)  = "SCNuPTR"
builtin_fmt_rd (Size)    = "SCNuSIZE"
builtin_fmt_rd (Bool)    = "\"i\""
builtin_fmt_rd (String)  = "\"s\""
builtin_fmt_rd (Char)    = "\"c\""

builtin_get_bits:: TypeBuiltIn -> Integer
builtin_get_bits (UInt8)   = 8
builtin_get_bits (UInt16)  = 16
builtin_get_bits (UInt32)  = 32
builtin_get_bits (UInt64)  = 64
builtin_get_bits (UIntPtr) = 64 -- xxx: make this arch specific!
builtin_get_bits (Int8)    = 8
builtin_get_bits (Int16)   = 16
builtin_get_bits (Int32)   = 32
builtin_get_bits (Int64)   = 64
builtin_get_bits (IntPtr)  = 64 -- xxx: make this arch specific!
builtin_get_bits (Size)    = 64 -- xxx: make this arch specific!
builtin_get_bits (Bool)    = 8
builtin_get_bits (Char)    = 8
