/**
 * @name Returned pointer not checked
 * @description Dereferencing an untested value from a function that can return null may lead to undefined behavior.
 * @kind problem
 * @id cpp/missing-null-test
 * @problem.severity recommendation
 * @security-severity 7.5
 * @tags reliability
 *       security
 *       external/cwe/cwe-476
 */

// Copyright (c) 2026 Bubbles
// From Critical/MissingNullTest.ql in github/codeql, Copyright (c) GitHub, Inc.
// Licensed under The MIT License (MIT). Full text at:
//     https://mit-license.org/
// SPDX-License-Identifier: MIT

// The pack's Critical/MissingNullTest.ql, taught 3 GLib idioms that made
// nearly every pointer read as maybe NULL. Backlog 2026112310055945.
//  - g_strdup is an inline since GLib 2.80 whose first line returns NULL for a
//    constant NULL argument. It's NULL only when its argument is, as the pack
//    already says for CVS's Xstrdup.
//  - The NULL from g_return_val_if_fail is a caller bug, which GLib reports
//    as a critical, never a result a caller should test for.
//  - A function whose only NULL returns sit under `if (p == NULL)` or
//    `if (!p)` for a parameter p, such as eel_str_middle_truncate, is NULL
//    only when that argument is. That's g_strdup's case again, written out.
//    Backlog 2026112310061005.
//  - A failed g_assert_nonnull and the like don't come back. GLib marks
//    g_assertion_message and its kin G_ANALYZER_NORETURN, which only clang's
//    analyzer and Coverity see. In an optimized build G_LIKELY puts the test
//    in a statement expression, where the query loses it, so the assert
//    itself counts as the test there. The same goes for g_return_if_fail
//    and g_return_val_if_fail, which return before the read.

import cpp
import Options
import semmle.code.cpp.controlflow.Nullness
import semmle.code.cpp.controlflow.Dominance
import semmle.code.cpp.controlflow.SSA

predicate isStrdup(Function f) { f.hasGlobalName(["g_strdup_inline", "g_strdup"]) }

predicate badCallReturn(ReturnStmt ret) {
  exists(MacroInvocation mi |
    mi.getMacroName() = ["g_return_val_if_fail", "g_return_val_if_reached"] and
    mi.getAnAffectedElement() = ret
  )
}

// Its only NULL literal returns are g_return_val_if_fail's. A literal, since
// nullValue goes back through callMayReturnNull, which can't be negated here.
predicate nullOnBadCallOnly(Function f) {
  not isStrdup(f) and
  exists(ReturnStmt ret | ret.getEnclosingFunction() = f and badCallReturn(ret)) and
  not exists(ReturnStmt ret |
    ret.getEnclosingFunction() = f and ret.getExpr().getValue() = "0" and not badCallReturn(ret)
  ) and
  not f.hasGlobalOrStdName(["malloc", "calloc"])
}

// `if (p == NULL)`, `if (NULL == p)` or `if (!p)`, with ret in its then branch.
predicate underNullParamTest(ReturnStmt ret, Parameter p) {
  exists(IfStmt test, Expr cond |
    cond = test.getCondition() and
    test.getThen() = ret.getParent*() and
    (
      cond.(EQExpr).getAnOperand().(VariableAccess).getTarget() = p and
      cond.(EQExpr).getAnOperand().getValue() = "0"
      or
      cond.(NotExpr).getOperand().(VariableAccess).getTarget() = p
    )
  )
}

predicate literalNullReturn(ReturnStmt ret, Function f) {
  ret.getEnclosingFunction() = f and ret.getExpr().getValue() = "0"
}

// Every NULL literal it returns is under a test of parameter i, or is
// g_return_val_if_fail's. Literals only, as for nullOnBadCallOnly.
predicate nullOnNullArgOnly(Function f, int i) {
  not isStrdup(f) and
  not f.hasGlobalOrStdName(["malloc", "calloc"]) and
  exists(Parameter p | p = f.getParameter(i) and
    exists(ReturnStmt ret | literalNullReturn(ret, f) and underNullParamTest(ret, p)) and
    forall(ReturnStmt ret | literalNullReturn(ret, f) |
      underNullParamTest(ret, p) or badCallReturn(ret)
    )
  )
}

// Another maybe NULL value it returns, past those literals.
predicate otherNullReturnPastArgTest(Function f) {
  exists(ReturnStmt ret |
    ret.getEnclosingFunction() = f and nullValue(ret.getExpr()) and
    not literalNullReturn(ret, f)
  )
  or
  exists(ReturnStmt ret, Expr returned, ControlFlowNode init, LocalScopeVariable v |
    ret.getExpr() = returned and
    nullInit(v, init) and
    definitionUsePair(v, init, returned) and
    not checkedValid(v, returned) and
    ret.getEnclosingFunction() = f
  )
}

// What mayReturnNull still finds there: another maybe NULL value returned, or
// a NULL-initialized local returned untested.
predicate otherNullReturn(Function f) {
  exists(ReturnStmt ret |
    ret.getEnclosingFunction() = f and nullValue(ret.getExpr()) and not badCallReturn(ret)
  )
  or
  exists(ReturnStmt ret, Expr returned, ControlFlowNode init, LocalScopeVariable v |
    ret.getExpr() = returned and
    nullInit(v, init) and
    definitionUsePair(v, init, returned) and
    not checkedValid(v, returned) and
    ret.getEnclosingFunction() = f
  )
}

// g_assert_nonnull (p), or g_assert, g_return_if_fail or g_return_val_if_fail
// of p != NULL, with p not set again since.
predicate assertedNonNull(VariableAccess access) {
  exists(MacroInvocation mi, VariableAccess checked, SsaDefinition def, StackVariable v |
    (
      mi.getMacroName() = "g_assert_nonnull" and mi.getUnexpandedArgument(0) = v.getName()
      or
      mi.getMacroName() = ["g_assert", "g_return_if_fail", "g_return_val_if_fail"] and
      mi.getUnexpandedArgument(0) = v.getName() + " != NULL"
    ) and
    mi.getAnAffectedElement() = checked and
    checked = def.getAUse(v) and
    access = def.getAUse(v) and
    dominates(checked, access)
  )
}

class GLibOptions extends CustomOptions {
  override predicate overrideReturnsNull(Call call) {
    CustomOptions.super.overrideReturnsNull(call)
    or
    isStrdup(call.(FunctionCall).getTarget())
    or
    nullOnBadCallOnly(call.(FunctionCall).getTarget())
    or
    nullOnNullArgOnly(call.(FunctionCall).getTarget(), _)
  }

  override predicate exits(Function f) {
    CustomOptions.super.exits(f)
    or
    f.getName().matches("g\\_assertion\\_message%")
  }

  override predicate returnsNull(Call call) {
    CustomOptions.super.returnsNull(call)
    or
    isStrdup(call.(FunctionCall).getTarget()) and nullValue(call.getArgument(0))
    or
    nullOnBadCallOnly(call.(FunctionCall).getTarget()) and
    otherNullReturn(call.(FunctionCall).getTarget())
    or
    exists(int i |
      nullOnNullArgOnly(call.(FunctionCall).getTarget(), i) and nullValue(call.getArgument(i))
    )
    or
    nullOnNullArgOnly(call.(FunctionCall).getTarget(), _) and
    otherNullReturnPastArgTest(call.(FunctionCall).getTarget())
  }
}

from VariableAccess access
where
  maybeNull(access) and
  dereferenced(access) and
  not assertedNonNull(access)
select access, "Value may be null; it should be checked before dereferencing."
