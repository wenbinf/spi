/*
 * Copyright (c) 1996-2011 Barton P. Miller
 *
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 *
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include "agent/context.h"
#include "agent/patchapi/point.h"
#include "agent/propeller.h"
#include "agent/snippet.h"
#include "common/utils.h"

#include "patchAPI/h/Command.h"
#include "patchAPI/h/PatchMgr.h"

namespace sp {

  extern SpContext* g_context;
  extern SpParser::ptr g_parser;

  SpPropeller::SpPropeller() {
  }

  SpPropeller::ptr
  SpPropeller::Create() {
    return ptr(new SpPropeller);
  }

  // Instrument all "interesting points" inside `func`.
  // "Interesting points" are provided by SpPropeller::next_points.
  // By default, next_points provides all call instructions of `func`.
  // However, users can inherit SpPropeller and implement their own
  // next_points().
  bool
  SpPropeller::go(SpFunction* func,
                  PayloadFunc entry,
                  PayloadFunc exit,
                  SpPoint* point,
                  StringSet* inst_calls) {
    assert(func);

    sp_debug("START PROPELLING - propel to callees of function %s",
             func->name().c_str());

    // 1. Find points according to type
    Points pts;
    ph::PatchMgrPtr mgr = g_parser->mgr();
    assert(mgr);
    ph::PatchFunction* cur_func = NULL;
    if (point) {
      cur_func = func;
    } else {
      cur_func = g_parser->FindFunction(func->GetMangledName());
    }

    if (!cur_func) return false;

    next_points(cur_func, mgr, pts);

    // 2. Start instrumentation
    ph::Patcher patcher(mgr);
    for (unsigned i = 0; i < pts.size(); i++) {
      SpPoint* p = PT_CAST(pts[i]);
      assert(p);
      SpBlock* blk = p->GetBlock();
      assert(blk);

      if (blk->instrumented()) {
        continue;
      }
      
      // It's possible that callee will be NULL, which is an indirect call.
      // In this case, we'll parse it later during runtime.

      SpFunction* callee = g_parser->callee(p);

      if (callee) {
        if (!g_parser->CanInstrumentFunc(callee->name())) {
          sp_debug("SKIP NOT-INST FUNC - %s", callee->name().c_str());
          continue;
        }

        // Only works for direct function calls
        if (inst_calls &&
            (inst_calls->find(callee->name()) == inst_calls->end())) {
          // sp_print("SKIP NOT-INST CALL - %s", callee->name().c_str());
          sp_debug("SKIP NOT-INST CALL - %s", callee->name().c_str());
          continue;
        }
        sp_debug("POINT - instrumenting direct call at %lx to "
                 "function %s (%lx) for point %lx",
                 blk->last(), callee->name().c_str(),
                 (dt::Address)callee, (dt::Address)p);
      } else {
        if (inst_calls) {
          // sp_print("SKIP INDIRECT CALL - at %lx", blk->last());
          sp_debug("SKIP INDIRECT CALL - at %lx", blk->last());
          continue;
        }
        sp_debug("POINT - instrumenting indirect call at %lx for point %lx",
                 blk->last(), (dt::Address)p);
      }

      sp_debug("PAYLOAD ENTRY - %lx", (long)entry);
      p->SetCallerPt(point);
      SpSnippet::ptr sp_snip = SpSnippet::create(callee,
                                                 p,
                                                 entry,
                                                 exit);
      /*
      ph::Snippet<SpSnippet::ptr>::Ptr snip =
        ph::Snippet<SpSnippet::ptr>::create(sp_snip);
      assert(sp_snip && snip);
      p->SetSnip(sp_snip);
      patcher.add(ph::PushBackCommand::create(p, snip));
      */
      assert(sp_snip);
      p->SetSnip(sp_snip);
      patcher.add(ph::PushBackCommand::create(p, sp_snip));
    }
    bool ret = patcher.commit();

    if (ret) {
      sp_debug("FINISH PROPELLING - callees of function %s are"
               " instrumented", func->name().c_str());
    } else {
      sp_debug("FINISH PROPELLING - instrumentation failed for"
               " callees of %s", func->name().c_str());
    }
    return ret;
  }

  // Find all PreCall points
  void
  SpPropeller::next_points(ph::PatchFunction* cur_func,
                           ph::PatchMgrPtr mgr,
                           Points& pts) {
    ph::Scope scope(cur_func);
    mgr->findPoints(scope, ph::Point::PreCall, back_inserter(pts));
  }

}
