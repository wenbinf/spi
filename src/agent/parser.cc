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

#include <sys/shm.h>
#include <sys/mman.h>
#include <dirent.h>
#include <stack>

#include "instructionAPI/h/BinaryFunction.h"
#include "instructionAPI/h/Immediate.h"
#include "instructionAPI/h/Visitor.h"
#include "patchAPI/h/PatchMgr.h"
#include "symtabAPI/h/AddrLookup.h"
#include "symtabAPI/h/Symtab.h"

#include "agent/context.h"
#include "agent/parser.h"
#include "agent/patchapi/addr_space.h"
#include "agent/patchapi/instrumenter.h"
#include "agent/patchapi/maker.h"
#include "agent/patchapi/object.h"
#include "agent/patchapi/point.h"
#include "common/utils.h"
#include "injector/injector.h"



namespace sp {

extern SpContext* g_context;
extern SpParser::ptr g_parser;

SpParser::SpParser()
    : injected_(false), exe_obj_(NULL) {

  binaries_to_inst_.insert(sp_filename(GetExeName().c_str()));
}

// Clean up memory buffers for ParseAPI stuffs
SpParser::~SpParser() {
  for (CodeSources::iterator i = code_srcs_.begin();
       i != code_srcs_.end(); i++) {
    delete static_cast<pe::SymtabCodeSource*>(*i);
  }
  for (CodeObjects::iterator i = code_objs_.begin();
       i != code_objs_.end(); i++) {
    delete *i;
  }

  // Destory shared memory we use
  shmctl(IJMSG_ID, IPC_RMID, NULL);
}

// Agent-writer can create their own Parser. So use shared point to make
// memory management easy
SpParser::ptr
SpParser::Create() {
  return ptr(new SpParser);
}

// The main parsing routine.
// This is the default implementation, which parses binary during
// runtime.
// Agent-writer can provide their own parse() method.
ph::PatchMgrPtr
SpParser::Parse() {
  // Avoid duplicate parsing. If a shared library is loaded during
  // runtime, we have to use PatchAPI's loadLibrary method to add
  // this shared library
  if (mgr_) return mgr_;

  // Step 1: Parse runtime symtabs
  SymtabSet unique_tabs;
  sb::AddressLookup* al = GetRuntimeSymtabs(unique_tabs);
  if (!al) {
    sp_perror("FAILED TO GET RUNTIME SYMTABS");
  }
  if (agent_name_.size() == 0) {
    agent_name_ = "libagent.so";
  }
  binaries_to_inst_.insert(agent_name_);

  // Step 2: Create patchapi objects
  PatchObjects patch_objs;
  if (!CreatePatchobjs(unique_tabs, al, patch_objs)) {
    sp_perror("FAILED TO CREATE PATCHOBJS");
  }

  // Step 3: In the case of executable shared library, we cannot get
  // exe_obj_ via symtabAPI, so we resort to /proc
  if (!exe_obj_) {
    exe_obj_ = GetExeFromProcfs(patch_objs);
    if (!exe_obj_) {
      sp_perror("FAILED TO GET EXE OBJ FROM PROC FS");
    }
  } // !exe_obj_

  // Step 4: Initialize PatchAPI stuffs
  mgr_ = CreateMgr(patch_objs);
  if (!mgr_) {
    sp_perror("FAILED TO CREATE PATCHMGR");
  }

  return mgr_;
}

// Find the function that contains addr
ph::PatchFunction*
SpParser::FindFunction(dt::Address addr) {
  sp_debug("FIND FUNC BY ADDR - for call insn %lx", addr);
  ph::AddrSpace* as = mgr_->as();
  for (ph::AddrSpace::ObjMap::iterator ci = as->objMap().begin();
       ci != as->objMap().end(); ci++) {
    SpObject* obj = static_cast<SpObject*>(ci->second);

    pe::CodeObject* co = obj->co();
    pe::CodeSource* cs = co->cs();

    set<pe::CodeRegion *> match;
    set<pe::Block *> blocks;
    int cnt = cs->findRegions(addr, match);
    if(cnt != 1)
      continue;
    else if(cnt == 1) {
      co->findBlocks(*match.begin(), addr, blocks);

      sp_debug("%ld blocks found", (long)blocks.size());
      if (blocks.size() == 1) {
        std::vector<pe::Function*> funcs;
        (*blocks.begin())->getFuncs(funcs);
        return obj->getFunc(*funcs.begin());
      }
      return NULL;
    }
  }
  return NULL;
}

typedef struct {
  char libname[512];
  char err[512];
  char loaded;
  long pc;
  long sp;
  long bp;
} IjMsg;

// Get function address from function name.
dt::Address
SpParser::GetFuncAddrFromName(string name) {
  ph::AddrSpace* as = mgr_->as();
  assert(as);
  for (ph::AddrSpace::ObjMap::iterator ci = as->objMap().begin();
       ci != as->objMap().end(); ci++) {
    SpObject* obj = OBJ_CAST(ci->second);
    assert(obj);
    sp_debug("GET FUNC ADDR - in object %s", obj->name().c_str());
    pe::CodeObject* co = obj->co();
    pe::CodeObject::funclist& all = co->funcs();
    for (pe::CodeObject::funclist::iterator fit = all.begin();
         fit != all.end(); fit++) {
      if ((*fit)->name().compare(name) == 0) {
        dt::Address addr = (*fit)->addr() + obj->codeBase();
        return addr;
      }
    }
  }
  sp_debug("GET FUNC ADDR - cannot find %s", name.c_str());
  return 0;
}

// Find function by name.
// If allow_plt = true, then we may return a plt entry;
// otherwise, we strictly skip plt entries.
ph::PatchFunction*
SpParser::FindFunction(string name, bool allow_plt) {
  sp_debug("LOOKING FOR FUNC - looking for %s", name.c_str());
  if (real_func_map_.find(name) != real_func_map_.end()) {
    sp_debug("GOT FROM CACHE - %s",
             real_func_map_[name]->name().c_str());
    return real_func_map_[name];
  }
  assert(mgr_);
  ph::AddrSpace* as = mgr_->as();
  FuncSet func_set;
  for (ph::AddrSpace::ObjMap::iterator ci = as->objMap().begin();
       ci != as->objMap().end(); ci++) {
    SpObject* obj = OBJ_CAST(ci->second);
    pe::CodeObject* co = obj->co();
    sb::Symtab* sym = obj->symtab();
    if (!sym) {
      sp_perror("Failed to get Symtab object");
    }

    sp_debug("IN OBJECT - %s", sym->name().c_str());

    if (!CanInstrument(sp_filename(sym->name().c_str()))) {
      sp_debug("SKIP - lib %s",
               sp_filename(sym->name().c_str()));
      continue;
    }

    pe::CodeObject::funclist& all = co->funcs();
    for (pe::CodeObject::funclist::iterator fit = all.begin();
         fit != all.end(); fit++) {

      if ((*fit)->name().compare(name) == 0) {
        sb::Region* region = sym->findEnclosingRegion((*fit)->addr());
        if (!allow_plt && region &&
            (region->getRegionName().compare(".plt") == 0)) {

          sp_debug("A PLT, SKIP - %s at %lx", name.c_str(),
                   (*fit)->addr());
          continue;
        }
        ph::PatchFunction* found = obj->getFunc(*fit);
        if (real_func_map_.find(name) == real_func_map_.end())
          real_func_map_[name] = found;
        sp_debug("GOT %s in OBJECT - %s", name.c_str(),
                 sym->name().c_str());
        func_set.insert(found);
      }
    } // For each function
  } // For each object

  if (func_set.size() == 1) {
    sp_debug("FOUND - %s in object %s", name.c_str(),
             FUNC_CAST((*func_set.begin()))->GetObject()->name().c_str());
    return *func_set.begin();
  }
  sp_debug("NO FOUND - %s", name.c_str());
  return NULL;
}

// Dump instructions in text.
string
SpParser::DumpInsns(void* addr, size_t size) {

  dt::Address base = (dt::Address)addr;
  pe::CodeSource* cs = exe_obj_->co()->cs();
  string s;
  char buf[256];
  in::InstructionDecoder deco(addr, size, cs->getArch());
  in::Instruction::Ptr insn = deco.decode();
  while(insn) {
    sprintf(buf, "    %lx(%2lu bytes): %-25s | ", base,
            (unsigned long)insn->size(), insn->format(base).c_str());
    char* raw = (char*)insn->ptr();
    for (unsigned i = 0; i < insn->size(); i++)
      sprintf(buf, "%s%2x ", buf, 0xff&raw[i]);
    sprintf(buf, "%s\n", buf);
    s += buf;
    base += insn->size();
    insn = deco.decode();
  }
  return s;
}

// To calculate absolute address for indirect function call
class SpVisitor : public in::Visitor {
 public:
  SpVisitor(sp::SpPoint* pt)
      : Visitor(), call_addr_(0), pt_(pt), use_pc_(false) { }
  virtual void visit(in::RegisterAST* r) {
    if (IsPcRegister(r->getID())) {
      use_pc_ = true;
      call_addr_ = pt_->block()->end();
      // call_addr_ = pt_->block()->last();
    } else {
      // Non-pc case, x86-32 always goes this way
      dt::Address rval = pt_->snip()->get_saved_reg(r->getID());
      call_addr_ = rval;
    }

    sp_debug("SP_VISITOR - reg value %lx", call_addr_);
    stack_.push(call_addr_);
  }
  virtual void visit(in::BinaryFunction* b) {
    dt::Address i1 = stack_.top();
    stack_.pop();
    dt::Address i2 = stack_.top();
    stack_.pop();

    if (b->isAdd()) {
      call_addr_ = i1 + i2;
      sp_debug("SP_VISITOR - %lx + %lx = %lx", i1, i2, call_addr_);
    } else if (b->isMultiply()) {
      call_addr_ = i1 * i2;
      sp_debug("SP_VISITOR - %lx * %lx = %lx", i1, i2, call_addr_);
    } else {
      assert(0);
    }

    stack_.push(call_addr_);
  }
  virtual void visit(in::Immediate* i) {
    in::Result res = i->eval();
    switch (res.size()) {
      case 1: {
        call_addr_ =res.val.u8val;
        break;
      }
      case 2: {
        call_addr_ =res.val.u16val;
        break;
      }
      case 4: {
        call_addr_ =res.val.u32val;
        break;
      }
      default: {
        call_addr_ =res.val.u64val;
        break;
      }
    }

    sp_debug("SP_VISITOR - imm %lx ", call_addr_);
    stack_.push(call_addr_);
  }
  virtual void visit(in::Dereference* d) {
    dt::Address* addr = (dt::Address*)stack_.top();
    stack_.pop();
    call_addr_ = *addr;
    sp_debug("SP_VISITOR - dereference %lx => %lx ",
             (dt::Address)addr, call_addr_);
    stack_.push(call_addr_);
  }

  dt::Address call_addr() const {
    return call_addr_;
  }
  bool use_pc() const {
    return use_pc_;
  }
 private:
  std::stack<dt::Address> stack_;
  dt::Address call_addr_;
  sp::SpPoint* pt_;
  bool use_pc_;
};

// TODO (wenbin): is it okay to cache indirect callee?
SpFunction*
SpParser::callee(SpPoint* pt,
                 bool parse_indirect) {
  assert(pt);
  // 0. Check the cache
  // TODO: Should always re-parse indirect call
  if (pt->callee()) return FUNC_CAST(pt->callee());

  // 1. Looking for direct call
  ph::PatchFunction* f = pt->getCallee();
  if (f) {

    ph::PatchFunction* tmp_f = g_parser->FindFunction(f->name());
    if (tmp_f && tmp_f != f)  f = tmp_f;

    SpFunction* sfunc = FUNC_CAST(f);
    assert(sfunc);
    pt->SetCallee(sfunc);
    return sfunc;
  }
  else if (pt->callee()) {
    return FUNC_CAST(pt->callee());
  }

  // 2. Looking for indirect call
  if (parse_indirect) {
    SpBlock* b = pt->GetBlock();
    assert(b);

    sp_debug("PARSING INDIRECT - for call insn %lx",
             b->last());

    in::Instruction::Ptr insn = b->orig_call_insn();
    assert(insn);

    sp_debug("DUMP INDCALL INSN (%ld bytes)- {", (long)insn->size());
    sp_debug("%s",
             DumpInsns((void*)insn->ptr(),
                       insn->size()).c_str());
    sp_debug("DUMP INSN - }");

    in::Expression::Ptr trg = insn->getControlFlowTarget();
    dt::Address call_addr = 0;
    if (trg) {
      SpVisitor visitor(pt);
      trg->apply(&visitor);
      call_addr = visitor.call_addr();
      f = FindFunction(call_addr);
      if (f) {
        SpFunction* sfunc = FUNC_CAST(f);
        assert(sfunc);
        sp_debug("PARSED INDIRECT - %lx is %s in %s", b->last(),
                 sfunc->name().c_str(),
                 sfunc->GetObject()->name().c_str());

        pt->SetCallee(sfunc);
        return sfunc;
      }
    }

    sp_debug("CANNOT FIND INDRECT CALL - for call insn %lx",
             b->last());

#if 0
    sp_print("DUMP INSN (%d bytes)- {", pt->snip()->size());
    sp_print("%s",
             dump_insn((void*)pt->snip()->buf(),
                       pt->snip()->size()).c_str());
    sp_print("DUMP INSN - }");
#endif
    return NULL;
  }

  return NULL;
}

void
SpParser::GetFrame(long* pc, long* sp, long* bp) {
  IjMsg* shm = (IjMsg*)GetSharedMemory(1986, sizeof(IjMsg));
  *pc = shm->pc;
  *sp = shm->sp;
  *bp = shm->bp;
}

// Get symtabs during runtime
// Side effect:
// 1. determine whether this agent is injected or preloaded
// 2. figure out the agent library's name
sb::AddressLookup*
SpParser::GetRuntimeSymtabs(SymtabSet& symtabs) {
  // Get all symtabs in this process
  sb::AddressLookup* al =
      sb::AddressLookup::createAddressLookup(getpid());
  if (!al) {
    sp_debug("FAILED TO CREATE AddressLookup");
    return NULL;
  }
  al->refresh();
  std::vector<sb::Symtab*> tabs;
  al->getAllSymtabs(tabs);
  if (tabs.size() == 0) {
    sp_debug("FOUND NO SYMTABS");
    return NULL;
  }
  sp_debug("SYMTABS - %ld symtabs found", (long)tabs.size());

  // Determine whether the agent is injected or preloaded.
  //   - true: injected by other process (libijagent.so is loaded)
  //   - false: preloaded

  // We rely on looking for the magic variable -
  // " int self_propelled_instrumentation_agent_library = 19861985; "
  string magic_var = "self_propelled_instrumentation_agent_library";

  for (std::vector<sb::Symtab*>::iterator i = tabs.begin();
       i != tabs.end(); i++) {
    sb::Symtab* sym = *i;

    // Figure the name for agent library
    Symbols syms;
    if (sym->findSymbol(syms, magic_var) && syms.size() > 0) {
      agent_name_ = sym->name();
      sp_debug("AGENT NAME - %s", agent_name_.c_str());
    }

    // Deduplicate symtabs
    symtabs.insert(sym);
    if (sym->name().find("libijagent.so") != string::npos) {
      injected_ = true;
    }
  }
  return al;
}

bool
SpParser::CreatePatchobjs(SymtabSet& unique_tabs,
                          sb::AddressLookup* al,
                          PatchObjects& patch_objs) {
  // The main loop to build PatchObject for all dependencies
  for (SymtabSet::iterator i = unique_tabs.begin();
       i != unique_tabs.end(); i++) {
    sb::Symtab* sym = *i;
    dt::Address load_addr = 0;
    al->getLoadAddress(sym, load_addr);

    // Parsing binary objects is very time consuming. We avoid
    // parsing the libraries that are either well known (e.g., libc,
    // or dyninst).
    string libname_no_path = sp_filename(sym->name().c_str());
    if (!CanInstrument(libname_no_path)) {
      sp_debug("SKIPED - skip parsing %s",
               sp_filename(sym->name().c_str()));
      continue;
    }

    ph::PatchObject* patch_obj = CreateObject(sym, load_addr);
    if (!patch_obj) {
      sp_debug("FAILED TO CREATE PATCH OBJECT");
      continue;
    }

    patch_objs.push_back(patch_obj);
    sp_debug("PARSED - parsed %s", sp_filename(sym->name().c_str()));

    if (sym->isExec()) {
      exe_obj_ = patch_obj;
      sp_debug("EXE - %s is an executable",
               sp_filename(sym->name().c_str()));
    }
  } // End of symtab iteration

  return true;
}

SpObject*
SpParser::GetExeFromProcfs(PatchObjects& patch_objs) {
  for (PatchObjects::iterator oi = patch_objs.begin();
       oi != patch_objs.end(); oi++) {
    SpObject* obj = OBJ_CAST(*oi);
    assert(obj);
    char* s1 = sp_filename(sp::GetExeName().c_str());
    char* s2 = sp_filename(obj->name().c_str());
    // sp_debug("PARSE EXE - s1=%s, s2 =%s", s1, s2);
    if (strcmp(s1, s2) == 0) {
      sp_debug("GOT EXE - %s is an executable shared library", s2);
      return obj;
    } // strcmp
  } // for each obj
  return NULL;
}

// Create a PatchMgr object.
ph::PatchMgrPtr
SpParser::CreateMgr(PatchObjects& patch_objs) {
  SpAddrSpace* as = SpAddrSpace::Create(exe_obj_);
  assert(as);

  SpInstrumenter* inst = sp::SpInstrumenter::create(as);
  assert(inst);

  ph::PatchMgrPtr mgr = ph::PatchMgr::create(as,
                                             inst,
                                             new SpPointMaker);
  assert(mgr);

  // Load each parsed objects into address space
  for (PatchObjects::iterator i = patch_objs.begin();
       i != patch_objs.end(); i++) {
    if (*i != exe_obj_) {
      as->loadObject(*i);
    }
  }

  // Initialize memory allocator
  as->InitMemoryAllocator();
  
  return mgr;
}

SpObject*
SpParser::CreateObject(sb::Symtab* sym,
                       dt::Address load_addr) {
  assert(sym);
  SpObject* obj = NULL;
  bool parse_from_file = false;
  char* no_path_name = sp_filename(sym->name().c_str());
  if (getenv("SP_PARSE_DIR")) {
    // Do name matching. Here we assume only one file hierarchy in
    // this directory $SP_PARSE_DIR
    DIR* dir = opendir(getenv("SP_PARSE_DIR"));
    if (dir) {
      dirent* entry = readdir(dir);
      while (entry) {
        sp_debug("Parsed library: %s is %s?", entry->d_name,
                 no_path_name);
        if (strcmp(no_path_name, entry->d_name) == 0) {
          sp_debug("GOT Parsed LIB: %s", entry->d_name);
          parse_from_file = true;
          break;
        }
        entry = readdir(dir);
      }
      closedir(dir);
    } else {
      sp_debug("FAILED TO ITERATE DIR %s", getenv("SP_PARSE_DIR"));
    }
  }

  if (parse_from_file) {
    sp_debug("PARSE FROM FILE - for %s", sym->name().c_str());
    obj = CreateObjectFromFile(sym, load_addr);
  } else {
    sp_debug("PARSE FROM RUNTIME - for %s", sym->name().c_str());
    obj = CreateObjectFromRuntime(sym, load_addr);
  }
  return obj;
}

SpObject*
SpParser::CreateObjectFromRuntime(sb::Symtab* sym,
                                  dt::Address load_addr) {

  // Parse binary objects using ParseAPI::CodeObject::parse().
  pe::SymtabCodeSource* scs = new pe::SymtabCodeSource(sym);
  code_srcs_.push_back(scs);
  pe::CodeObject* co = new pe::CodeObject(scs);
  code_objs_.push_back(co);
  co->parse();

  /*
    vector<CodeRegion *>::const_iterator rit = scs->regions().begin();
    for( ; rit != scs->regions().end(); ++rit) {
    SymtabCodeRegion * scr = static_cast<SymtabCodeRegion*>(*rit);
    if(scr->symRegion()->isText()) {
    co->parseGaps(scr);
    }
    }
  */

  dt::Address real_load_addr =
      load_addr ? load_addr : scs->loadAddress();

  SpObject* obj =  new sp::SpObject(co,
                                    load_addr,
                                    new SpCFGMaker,
                                    NULL,
                                    real_load_addr,
                                    sym);

  // TODO: If needed, we create a serialized object
  return obj;
}

SpObject*
SpParser::CreateObjectFromFile(sb::Symtab* sym,
                               dt::Address load_addr) {
  SpObject* obj = NULL;
  return obj;
}



void
SpParser::SetLibrariesToInstrument(const StringSet& libs) {
  for (StringSet::iterator i = libs.begin(); i != libs.end(); i++)
    binaries_to_inst_.insert(*i);
}

bool
SpParser::CanInstrument(string lib) {
  for (StringSet::iterator i = binaries_to_inst_.begin();
       i != binaries_to_inst_.end(); i++) {
    if (lib.find(*i) != string::npos) return true;
  }
  return false;
}

}
