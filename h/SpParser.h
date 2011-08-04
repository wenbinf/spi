#ifndef SP_PARSER_H_
#define SP_PARSER_H_

#include "PatchObject.h"
#include "PatchMgr.h"

namespace sp {

/*
   Parser is to parse the CFG structures of the mutatee process.
 */
class SpParser : public Dyninst::PatchAPI::CFGMaker {
  public:
    typedef dyn_detail::boost::shared_ptr<SpParser> ptr;
    virtual ~SpParser();
    static ptr create();

    typedef std::vector<Dyninst::ParseAPI::CodeObject*> CodeObjects;
    typedef std::vector<Dyninst::PatchAPI::PatchObject*> PatchObjects;
    virtual Dyninst::PatchAPI::PatchMgrPtr parse();
    Dyninst::PatchAPI::PatchObject* exe_obj();
    Dyninst::ParseAPI::Function* findFunction(Dyninst::Address addr);
  protected:
    typedef std::vector<Dyninst::ParseAPI::CodeSource*> CodeSources;
    CodeSources code_srcs_;
    CodeObjects code_objs_;
    Dyninst::PatchAPI::PatchObject* exe_obj_;
    Dyninst::PatchAPI::PatchMgrPtr mgr_;

    SpParser();
};

}

#endif /* SP_PARSER_H_ */
