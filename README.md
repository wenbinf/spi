Install
-------
1. Copy example-make.config to be "config.mk".
2. Edit make.config to define each macro variable.
  - DYNINST_DIR: absolute path of Dyninst, which contains directories like
    dyninstAPI, parseAPI, ...
  - SP_DIR: absolute path of this package (the output of `pwd`), because this
    file is also used by Makefile, I don't put `pwd` in it.
  - DYNLINK: true for building shared library for agent, otherwise for building
    static library

Environment variables
---------------------

FOR DEBUGGING

1. SP_COREDUMP: enables core dump when segfault happens
2. SP_DEBUG: enables printing out debugging messages
3. SP_TEST_RELOCINSN: only uses instruction relocation instrumentation worker
4. SP_TEST_RELOCBLK: only uses call block relocation instrumentation worker
5. SP_TEST_SPRING: only uses sprint block instrumentation worker
6. SP_TEST_TRAP: only uses trap instrumentation worker
7. SP_NO_TAILCALL: don't instrument tail calls
8. SP_LIBC_MALLOC: will always use libc malloc
9. SP_NO_LIBC_MALLOC: will never use libc malloc

FOR RUNTIME

* PLATFORM: 'i386-unknown-linux2.4' for x86 or 'x86_64-unknown-linux2.4' for
          x86-64
* SP_DIR: the root directory of self-propelled instrumentation.
* SP_AGENT_DIR: the directory path of agent shared library that will be injected.

Shared memory id used
---------------------
* 1986: for communication between injector process and user process

Make arguments
--------------
- For testing
  - make unittest: build unittests
  - make mutatee: build simple mutatees
  - make external_mutatee: build real world mutatees
  - make test: unittest + mutatee + external_mutatee

- For main self propelled
  - make injector_exe
  - make agent_lib
  - make spi: agent_lib + injector_exe

- For everything
  - make / make all: spi + test

- For cleaning
  - make clean_test: clean test stuffs
  - make clean: only clean core self-propelled stuffs, excluding dependency
  - make clean_all: clean everything, including dependency
  - make clean_objs: clean core self-propelled objs

SSH
---
- To run test suite:
  - make "ssh localhost" work without typing in password
