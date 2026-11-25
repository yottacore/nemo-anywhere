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
//  - A failed g_assert_nonnull and the like don't come back. GLib marks
//    g_assertion_message and its kin G_ANALYZER_NORETURN, which only clang's
//    analyzer and Coverity see. In an optimized build G_LIKELY puts the test
//    in a statement expression, where the query loses it, so the assert
//    itself counts as the test there.

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

// g_assert_nonnull (p) or g_assert (p != NULL), with p not set again since.
predicate assertedNonNull(VariableAccess access) {
  exists(MacroInvocation mi, VariableAccess checked, SsaDefinition def, StackVariable v |
    (
      mi.getMacroName() = "g_assert_nonnull" and mi.getUnexpandedArgument(0) = v.getName()
      or
      mi.getMacroName() = "g_assert" and mi.getUnexpandedArgument(0) = v.getName() + " != NULL"
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
  }
}

from VariableAccess access
where
  maybeNull(access) and
  dereferenced(access) and
  not assertedNonNull(access)
select access, "Value may be null; it should be checked before dereferencing."
