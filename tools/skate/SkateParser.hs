{-
  SkateParser: The Skate file parser

  Part of Skate: a Schema specification languge

  Copyright (c) 2017, ETH Zurich.
  All rights reserved.

  This file is distributed under the terms in the attached LICENSE file.
  If you do not find this file, copies can be found by writing to:
  ETH Zurich D-INFK, Universit\"atstr. 6, CH-8092 Zurich. Attn: Systems Group.
-}
module SkateParser where

import Prelude
import Text.ParserCombinators.Parsec as Parsec
import Text.ParserCombinators.Parsec.Expr
import Text.ParserCombinators.Parsec.Pos
import qualified Text.ParserCombinators.Parsec.Token as P
import Text.ParserCombinators.Parsec.Language( javaStyle )
import Data.Char
import Numeric
import Data.List
import Text.Printf

import SkateTypes


{-
==============================================================================
= Helper functions
==============================================================================
-}

{- creates a qualified identifier of the form parent.identifier -}
make_qualified_identifer :: String -> String -> String
make_qualified_identifer "" i = i
make_qualified_identifer parent i = parent ++ "." ++ i

{-
==============================================================================
= Token data types
==============================================================================
-}

{- import data type -}
data Import = Import String

{- Facts -}
data FactAttrib = FactAttrib String String TypeRef

{- Flags -}
data FlagDef = FlagDef String String Integer

{- Constants -}
data ConstantDef = ConstantDefInt String String Integer
                 | ConstantDefStr String String String

{- Enumerations -}
data EnumDef = EnumDef String String


{- declarations -}
data Declaration = Fact String String [ FactAttrib ]
                  | Flags String String Integer [ FlagDef ]
                  | Constants String String TypeRef [ ConstantDef ]
                  | Enumeration String String [ EnumDef ]
                  | Namespace String String [ Declaration ]
                  | Section String [ Declaration ]
                  | Text String

{--}
instance Show Declaration where
    show de@(Fact i d _) = "Fact '" ++ i ++ "'"
    show de@(Flags  i d _ _) = "Flags '" ++ i ++ "'"
    show de@(Enumeration  i d _) = "Enumeration '" ++ i ++ "'"
    show de@(Constants  i d _ _) = "Constants '" ++ i ++ "'"
    show de@(Namespace  i d _) = "Namespace '" ++ i ++ "'"
    show de@(Section  i _) = "Section '" ++ i ++ "'"
    show de@(Text i) = "Text Block"

{- the schema -}
data Schema = Schema String String [ Declaration ] [ String ]


{-
==============================================================================
= The Skate Token Parser
==============================================================================
-}

{- create the Skate Lexer -}
lexer = P.makeTokenParser (
    javaStyle  {
        {- list of reserved Names -}
        P.reservedNames = [
            "schema", "fact",
            "flags", "flag",
            "constants", "const",
            "enumeration", "enum",
            "text", "section"
        ],
        {- list of reserved operators -}
        P.reservedOpNames = ["*","/","+","-"],

        {- valid identifiers -}
        P.identStart = letter,
        P.identLetter = alphaNum,

        {- Skate is not case sensitive. -}
        P.caseSensitive = False,

        {- comment start and end -}
        P.commentStart = "/*",
        P.commentEnd = "*/",
        P.commentLine = "//",
        P.nestedComments = False
    })

{- Token definitions -}
whiteSpace = P.whiteSpace lexer
reserved   = P.reserved lexer
identifier = P.identifier lexer
stringLit  = P.stringLiteral lexer
comma      = P.comma lexer
commaSep   = P.commaSep lexer
commaSep1  = P.commaSep1 lexer
parens     = P.parens lexer
braces     = P.braces lexer
squares    = P.squares lexer
semiSep    = P.semiSep lexer
symbol     = P.symbol lexer
natural    = P.natural lexer
integer    = try ((P.lexeme lexer) binLiteral)
             <|> P.integer lexer

{- Parsing an integer number -}
binDigit = oneOf "01"
binLiteral = do {
    _ <- char '0';
    _ <- oneOf "bB";
    digits <- many1 binDigit;
    let n = foldl (\x d -> 2*x + (digitToInt d)) 0 digits
    ; seq n (return (fromIntegral n))
}


{------------------------------------------------------------------------------
- Parser start point
------------------------------------------------------------------------------}

{- parse the the Skate file -}
parse = do {
    whiteSpace;
    imps <- many importfacts;
    reserved "schema";
    name <- identifier;
    desc <- option name stringLit;
    decls <- braces (many1 $ schemadecl name);
    _ <- symbol ";" <?> " ';' missing from end of " ++ name ++ " schema def";
    return (Schema name desc decls [i | (Import i) <- imps])
}


{------------------------------------------------------------------------------
- Token rules for the Schema
------------------------------------------------------------------------------}

schemadecl sn = factdecl sn  <|>  constantdecl sn   <|> flagsdecl sn    <|>
                enumdecl sn  <|>  namespacedecl sn  <|> sectiondecl sn  <|>
                textdecl


{------------------------------------------------------------------------------
- Imports
------------------------------------------------------------------------------}

importfacts = do {
    reserved "import";
    i <- identifier;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " import";
    return (Import i)
}


{------------------------------------------------------------------------------
- Namespace
------------------------------------------------------------------------------}

namespacedecl parent = do {
    reserved "namespace";
    i <- identifier;
    d <- stringLit;
    decls <- braces (many1 $ schemadecl (make_qualified_identifer parent i));
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " namespace";
    return (Namespace (make_qualified_identifer parent i) d decls);
}

{------------------------------------------------------------------------------
- Facts
------------------------------------------------------------------------------}

factdecl parent = do {
    reserved "fact";
    i <- identifier;
    d <- stringLit;
    f <- braces (many1 $ factattrib parent);
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " fact";
    return (Fact (make_qualified_identifer parent i) d f)
}

factattrib parent = do {
    t <- fieldType parent;
    i <- identifier;
    d <- stringLit;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " fact attribute";
    return (FactAttrib i d t)
}


{------------------------------------------------------------------------------
- Flags
------------------------------------------------------------------------------}

flagsdecl parent = do {
    reserved "flags";
    i <- identifier;
    b <- integer;
    d <- stringLit;
    flagvals <- braces (many1 flagvals);
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " flags";
    return (Flags (make_qualified_identifer parent i) d b flagvals)
}

{- identifier = value "opt desc"; -}
flagvals = do {
    p <- integer;
    i <- identifier;
    d <- stringLit;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " flag val";
    return (FlagDef i d p)
};


{------------------------------------------------------------------------------
- Constants
------------------------------------------------------------------------------}


{- constants fname "desc" {[constantvals]}; -}
constantdecl parent = do {
    reserved "constants";
    i <- identifier;
    t <- fieldTypeBuiltIn;
    d <- stringLit;
    vals <- braces (many1 (constantvals t));
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " constants";
    return (Constants (make_qualified_identifer parent i) d t vals)
}

constantvals (TBuiltIn String) = constantvalsstring
constantvals (TBuiltIn UInt8) =  constantvalsnum
constantvals (TBuiltIn UInt16) =  constantvalsnum
constantvals (TBuiltIn UInt32)  =  constantvalsnum
constantvals (TBuiltIn UInt64)  =  constantvalsnum
constantvals (TBuiltIn UIntPtr) =  constantvalsnum
constantvals (TBuiltIn Int8) =  constantvalsnum
constantvals (TBuiltIn Int16) =  constantvalsnum
constantvals (TBuiltIn Int32) =  constantvalsnum
constantvals (TBuiltIn Int64)  =  constantvalsnum
constantvals (TBuiltIn IntPtr) =  constantvalsnum
constantvals (TBuiltIn Size) =  constantvalsnum
constantvals (TBuiltIn Char) =  constantvalsnum
constantvals (TBuiltIn Bool) =  constantvalsnum
constantvals s = error $ "Invalid constant type " ++ (show s)

constantvalsnum = do {
    i <- identifier;
    _ <- symbol "=";
    v <- integer;
    d <- stringLit;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " constant";
    return (ConstantDefInt i d v)
};

constantvalsstring = do {
    i <- identifier;
    _ <- symbol "=";
    v <- stringLit;
    d <- stringLit;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " constant";
    return (ConstantDefStr i d v)
};


{------------------------------------------------------------------------------
- Enumerations
------------------------------------------------------------------------------}

enumdecl parent = do {
    reserved "enumeration";
    i <- identifier;
    d <- stringLit;
    enums <- braces (many1 enumdef);
     _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " enumeration";
    return (Enumeration (make_qualified_identifer parent i) d enums)
}

enumdef = do {
    i <- identifier;
    d <- stringLit;
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " enum item";
    return (EnumDef i d)
};


{------------------------------------------------------------------------------
- Sections and Text blocks
------------------------------------------------------------------------------}

sectiondecl parent = do {
    reserved "section";
    i <- stringLit;
    decls <- braces (many1 $ schemadecl parent);
    _ <- symbol ";" <?> " ';' missing from end of " ++ i ++ " section";
    return (Section i decls);
};

textdecl = do {
    reserved "text";
    t <- braces (many1 stringLit);
    _ <- symbol ";" <?> " ';' missing from end of text block";
    return (Text (concat (intersperse " " t)) );
};


{------------------------------------------------------------------------------
Parsing Types
------------------------------------------------------------------------------}

fieldType p = fieldTypeFactRef p <|> fieldTypeConstRef p <|>
              fieldTypeEnumRef p <|> fieldTypeFlagsRef p <|> fieldTypeBuiltIn

fieldTypeBuiltIn = do {
    n <- identifier;
    return (TBuiltIn (findBuiltIntType n))
};

{- Parsing qualified identifiers -}
qualifiedPart = do {
    symbol ".";
    i <- identifier;
    return ("." ++ i);
}

qualifiedIdentiferLiteral p = do {
    i <- identifier;
    ids <- many qualifiedPart;
    return (i ++ (concat ids))
}

fieldTypeFactRef p  = do {
    reserved "fact";
    n <- qualifiedIdentiferLiteral p;
    return (TFact n p);
}

fieldTypeConstRef p  = do {
    reserved "const";
    n <- qualifiedIdentiferLiteral p;
    return (TConstant n p);
}

fieldTypeEnumRef p  = do {
    reserved "enum";
    n <- qualifiedIdentiferLiteral p;
    return (TEnum n p);
}

fieldTypeFlagsRef p = do {
    reserved "flag";
    n <- qualifiedIdentiferLiteral p;
    return (TFlags n p);
}
