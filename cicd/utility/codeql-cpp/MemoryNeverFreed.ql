/**
 * @name Memory is never freed
 * @description A function always returns before freeing memory that was allocated in the function. Freeing all memory allocated in the function before returning ties the lifetime of the memory blocks to that of the function call, making it easier to avoid and detect memory leaks.
 * @kind problem
 * @id cpp/memory-never-freed
 * @problem.severity warning
 * @security-severity 7.5
 * @tags efficiency
 *       security
 *       external/cwe/cwe-401
 */

// Copyright (c) 2026 Bubbles
// From Critical/MemoryNeverFreed.ql and Critical/MemoryFreed.qll in
// github/codeql, Copyright (c) GitHub, Inc.
// Licensed under The MIT License (MIT). Full text at:
//     https://mit-license.org/
// SPDX-License-Identifier: MIT

// The pack's Critical/MemoryNeverFreed.ql, taught 3 GLib idioms the points-to
// library can't follow. Backlog 2026112511381004.
//  - A g_autofree variable is freed by its cleanup function when it goes out
//    of scope, with no free call in sight.
//  - In an optimized build g_new and g_renew are statement expressions whose
//    value is a local holding the allocation, and points-to stops there.
//    So the statement expression's own value is what has to reach a free.
//  - A pointer handed to a GLib container or call that takes it, such as
//    g_ptr_array_add or g_bytes_new_take, is freed with that.

import cpp
import semmle.code.cpp.pointsto.PointsTo

private predicate autoFreed(Expr e) {
  exists(LocalVariable v | v.getAnAttribute().hasName("cleanup") |
    e = v.getAnAccess() or e = v.getInitializer().getExpr()
  )
}

private predicate takenOver(Expr e) {
  exists(FunctionCall c, string name, int i |
    c.getTarget().hasGlobalName(name) and e = c.getArgument(i)
  |
    name =
      [
        "g_ptr_array_add", "g_hash_table_add", "g_list_append", "g_list_prepend", "g_slist_append",
        "g_slist_prepend", "g_queue_push_tail", "g_queue_push_head", "g_task_return_pointer"
      ] and
    i = 1
    or
    name = ["g_hash_table_insert", "g_hash_table_replace"] and i = [1, 2]
    or
    name = ["g_ptr_array_insert", "g_object_set_data_full"] and i = 2
    or
    name = "g_bytes_new_take" and i = 0
  )
}

private predicate freed(Expr e) {
  e = any(DeallocationExpr de).getFreedExpr()
  or
  exists(ExprCall c |
    // cautiously assume that any `ExprCall` could be a deallocation expression.
    c.getAnArgument() = e
  )
  or
  autoFreed(e)
  or
  takenOver(e)
}

class FreedExpr extends PointsToExpr {
  FreedExpr() { freed(this) }

  override predicate interesting() { freed(this) }
}

private int freedSet() {
  exists(FreedExpr e | pointstosets(result, unresolveElement(e)))
  or
  setflow(result, freedSet())
}

// GLib's _G_NEW: ({ ...; __p = g_malloc (...); __p; })
private predicate inStmtExpr(AllocationExpr alloc, StmtExpr se) {
  exists(LocalVariable p, Assignment a |
    a.getRValue().getUnconverted() = alloc and
    a.getLValue() = p.getAnAccess() and
    se.getResultExpr().getUnconverted() = p.getAnAccess() and
    a.getEnclosingStmt().getParentStmt+() = se.getStmt()
  )
}

predicate allocMayBeFreed(AllocationExpr alloc) {
  anythingPointsTo(alloc)
  or
  exists(StmtExpr se | inStmtExpr(alloc, se) |
    pointstosets(freedSet(), unresolveElement(se.getFullyConverted()))
  )
}

from AllocationExpr alloc
where
  alloc.requiresDealloc() and
  not allocMayBeFreed(alloc)
select alloc, "This memory is never freed."
