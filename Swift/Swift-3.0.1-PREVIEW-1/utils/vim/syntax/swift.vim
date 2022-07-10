" Vim syntax file
" Language: swift
" Maintainer: Joe Groff <jgroff@apple.com>
" Last Change: 2013 Feb 2

if exists("b:current_syntax")
    finish
endif

syn keyword swiftKeyword
      \ associatedtype
      \ break
      \ case
      \ catch
      \ continue
      \ default
      \ defer
      \ do
      \ else
      \ fallthrough
      \ for
      \ guard
      \ if
      \ in
      \ let
      \ repeat
      \ return
      \ switch
      \ throw
      \ try
      \ typealias
      \ var
      \ where
      \ while
syn match swiftMultiwordKeyword
      \ "indirect case"

syn keyword swiftImport skipwhite nextgroup=swiftImportModule
      \ import

syn keyword swiftDefinitionModifier
      \ convenience
      \ dynamic
      \ fileprivate
      \ final
      \ internal
      \ nonmutating
      \ override
      \ private
      \ public
      \ required
      \ rethrows
      \ static
      \ throws

syn keyword swiftIdentifierKeyword
      \ Self
      \ metatype
      \ self
      \ super

syn keyword swiftFuncKeywordGeneral skipwhite nextgroup=swiftTypeParameters
      \ init

syn keyword swiftFuncKeyword
      \ deinit
      \ subscript

syn keyword swiftScope
      \ autoreleasepool

syn keyword swiftMutating skipwhite nextgroup=swiftFuncDefinition
      \ mutating
syn keyword swiftFuncDefinition skipwhite nextgroup=swiftTypeName,swiftOperator
      \ func

syn keyword swiftTypeDefinition skipwhite nextgroup=swiftTypeName
      \ class
      \ enum
      \ extension
      \ protocol
      \ struct
      \ typealias

syn keyword swiftVarDefinition skipwhite nextgroup=swiftVarName
      \ let
      \ var

syn keyword swiftLabel
      \ get
      \ set

syn keyword swiftBoolean
      \ false
      \ true

syn keyword swiftNil
      \ nil

syn match swiftImportModule contained nextgroup=swiftImportComponent
      \ /\<[A-Za-z_][A-Za-z_0-9]*\>/
syn match swiftImportComponent contained nextgroup=swiftImportComponent
      \ /\.\<[A-Za-z_][A-Za-z_0-9]*\>/

syn match swiftTypeName contained nextgroup=swiftTypeParameters
      \ /\<[A-Za-z_][A-Za-z_0-9\.]*\>/
syn match swiftVarName contained skipwhite nextgroup=swiftTypeDeclaration
      \ /\<[A-Za-z_][A-Za-z_0-9]*\>/
syn match swiftImplicitVarName
      \ /\$\<[A-Za-z_0-9]\+\>/

" TypeName[Optionality]?
syn match swiftType contained nextgroup=swiftTypeParameters
      \ /\<[A-Za-z_][A-Za-z_0-9\.]*\>[!?]\?/
" [Type:Type] (dictionary) or [Type] (array)
syn region swiftType contained contains=swiftTypePair,swiftType
      \ matchgroup=Delimiter start=/\[/ end=/\]/
syn match swiftTypePair contained nextgroup=swiftTypeParameters,swiftTypeDeclaration
      \ /\<[A-Za-z_][A-Za-z_0-9\.]*\>[!?]\?/
" (Type[, Type]) (tuple)
" FIXME: we should be able to use skip="," and drop swiftParamDelim
syn region swiftType contained contains=swiftType,swiftParamDelim
      \ matchgroup=Delimiter start="[^@](" end=")" matchgroup=NONE skip=","
syn match swiftParamDelim contained
      \ /,/
" <Generic Clause> (generics)
syn region swiftTypeParameters contained contains=swiftVarName,swiftConstraint
      \ matchgroup=Delimiter start="<" end=">" matchgroup=NONE skip=","
syn keyword swiftConstraint contained
      \ where

syn match swiftTypeDeclaration skipwhite nextgroup=swiftType
      \ /:/
syn match swiftTypeDeclaration skipwhite nextgroup=swiftType
      \ /->/

syn region swiftString start=/"/ skip=/\\\\\|\\"/ end=/"/ contains=swiftInterpolation
syn region swiftInterpolation start=/\\(/ end=/)/ contained
syn region swiftComment start="/\*" end="\*/" contains=swiftComment,swiftLineComment,swiftTodo
syn region swiftLineComment start="//" end="$" contains=swiftComment,swiftTodo

syn match swiftDecimal /[+\-]\?\<\([0-9][0-9_]*\)\([.][0-9_]*\)\?\([eE][+\-]\?[0-9][0-9_]*\)\?\>/
syn match swiftHex /[+\-]\?\<0x[0-9A-Fa-f][0-9A-Fa-f_]*\(\([.][0-9A-Fa-f_]*\)\?[pP][+\-]\?[0-9][0-9_]*\)\?\>/
syn match swiftOct /[+\-]\?\<0o[0-7][0-7_]*\>/
syn match swiftBin /[+\-]\?\<0b[01][01_]*\>/

syn match swiftOperator +\.\@<!\.\.\.\@!\|[/=\-+*%<>!&|^~]\@<!\(/[/*]\@![/=\-+*%<>!&|^~]*\|*/\@![/=\-+*%<>!&|^~]*\|->\@![/=\-+*%<>!&|^~]*\|[=+%<>!&|^~][/=\-+*%<>!&|^~]*\)+ skipwhite nextgroup=swiftTypeParameters
syn match swiftOperator "\.\.[<.]" skipwhite nextgroup=swiftTypeParameters

syn match swiftChar /'\([^'\\]\|\\\(["'tnr0\\]\|x[0-9a-fA-F]\{2}\|u[0-9a-fA-F]\{4}\|U[0-9a-fA-F]\{8}\)\)'/

syn match swiftPreproc /#\(\<file\>\|\<line\>\)/
syn match swiftPreproc /^\s*#\(\<if\>\|\<else\>\|\<elseif\>\|\<endif\>\)/
syn region swiftPreprocFalse start="^\s*#\<if\>\s\+\<false\>" end="^\s*#\(\<else\>\|\<elseif\>\|\<endif\>\)"

syn match swiftAttribute /@\<\w\+\>/ skipwhite nextgroup=swiftType

syn keyword swiftTodo MARK TODO FIXME contained

syn match swiftCastOp "\<is\>" skipwhite nextgroup=swiftType
syn match swiftCastOp "\<as\>[!?]\?" skipwhite nextgroup=swiftType

syn match swiftNilOps "??"

syn region swiftReservedIdentifier oneline
      \ start=/`/ end=/`/

hi def link swiftImport Include
hi def link swiftImportModule Title
hi def link swiftImportComponent Identifier
hi def link swiftKeyword Statement
hi def link swiftMultiwordKeyword Statement
hi def link swiftTypeDefinition Define
hi def link swiftType Type
hi def link swiftTypePair Type
hi def link swiftTypeName Function
hi def link swiftConstraint Special
hi def link swiftFuncDefinition Define
hi def link swiftDefinitionModifier Define
hi def link swiftFuncKeyword Function
hi def link swiftFuncKeywordGeneral Function
hi def link swiftVarDefinition Define
hi def link swiftVarName Identifier
hi def link swiftImplicitVarName Identifier
hi def link swiftIdentifierKeyword Identifier
hi def link swiftTypeDeclaration Delimiter
hi def link swiftTypeParameters Delimiter
hi def link swiftBoolean Boolean
hi def link swiftString String
hi def link swiftInterpolation Special
hi def link swiftComment Comment
hi def link swiftLineComment Comment
hi def link swiftDecimal Number
hi def link swiftHex Number
hi def link swiftOct Number
hi def link swiftBin Number
hi def link swiftOperator Function
hi def link swiftChar Character
hi def link swiftLabel Operator
hi def link swiftMutating Statement
hi def link swiftPreproc PreCondit
hi def link swiftPreprocFalse Comment
hi def link swiftAttribute Type
hi def link swiftTodo Todo
hi def link swiftNil Constant
hi def link swiftCastOp Operator
hi def link swiftNilOps Operator
hi def link swiftScope PreProc

let b:current_syntax = "swift"
