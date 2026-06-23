#pragma once

#include "abelcore/source_span.h"

#include <QString>

namespace abel {

enum class TokenKind {
    End,
    Identifier,
    Integer,
    Float,
    String,
    Char,

    LParen, RParen,
    LBrace, RBrace,
    LBracket, RBracket,
    Comma, Semicolon, Colon,
    Dot, Arrow, Scope,

    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Bang,
    Equal, EqualEqual, BangEqual,
    Less, LessEqual, Greater, GreaterEqual,
    AndAnd, OrOr,
    Power, ModMod, MinOp, MaxOp, PipeForward,
    Ellipsis,

    KwFn, KwReturn, KwIf, KwElseif, KwElse, KwWhile, KwFor, KwIn, KwRepeat,
    KwBreak, KwContinue, KwTrue, KwFalse, KwNullptr,
    KwConst, KwStruct, KwInit, KwBackend, KwDebt, KwLambda,
    KwVector, KwFunc, KwAny, KwNew, KwDelete, KwCast, KwThis,
    KwEnum, KwType,
    KwExport, KwModule, KwUse, KwAs, KwPublic, KwPrivate,
};

struct Token {
    TokenKind kind = TokenKind::End;
    QString text;
    SourceSpan span;
};

QString tokenKindName(TokenKind kind);

} // namespace abel
