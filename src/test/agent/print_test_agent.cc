#include "SpInc.h"
#include <sys/resource.h>

using namespace Dyninst;
using namespace PatchAPI;
using namespace sp;


void test_entry(SpPoint* pt) {

  SpFunction* f = Callee(pt);
  if (!f) return;
  //sp_print("%s", f->GetPrettyName().c_str());
  sp_print("%s", f->name().c_str());
  // fprintf(stderr, "%s\n", f->name().c_str());
  //sp_print("%s", f->GetMangledName().c_str());

  sp::Propel(pt);
}

AGENT_INIT
void MyAgent() {
  sp::SpAgent::ptr agent = sp::SpAgent::Create();
  // agent->EnableHandleDlopen(true);
  StringSet libs_to_inst;
  libs_to_inst.insert("libtest1.so");

  agent->SetLibrariesToInstrument(libs_to_inst);
  agent->SetInitEntry("test_entry");
  agent->Go();
}

AGENT_FINI
void DumpOutput() {
}
