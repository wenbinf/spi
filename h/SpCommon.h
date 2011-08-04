#ifndef SP_COMMON_H_
#define SP_COMMON_H_

#include <stdio.h>
#include <stdlib.h>

#include "dyn_detail/boost/shared_ptr.hpp"

/* Print facility */
#define sp_perror(...) do {\
  char* nodir = basename(__FILE__);                       \
  fprintf(stderr, "ERROR in %s [%d]: ", nodir, __LINE__); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  exit(0);\
} while(0)

#define sp_print(...) do {\
  fprintf(stdout, __VA_ARGS__); \
  fprintf(stdout, "\n"); \
  fflush(stderr); \
} while(0)

#define sp_debug(...) do { \
  if (getenv("SP_DEBUG")) {   \
      char* nodir = basename(__FILE__);              \
      fprintf(stderr, "%s [%d]: ", nodir, __LINE__); \
      fprintf(stderr, __VA_ARGS__); \
      fprintf(stderr, "\n");  \
      fflush(stderr); \
    } \
    else ; \
} while(0)

#define sp_filename(path) basename(path)

namespace sp {
class SpContext;
typedef dyn_detail::boost::shared_ptr<SpContext> SpContextPtr;
}

#define IJMSG_ID        1986
#define IJLIB_ID        1985
#define IJAGENT         "libijagent.so"
#endif /* SP_COMMON_H_ */