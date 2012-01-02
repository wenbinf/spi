#ifndef _SPIPCMGR_H_
#define _SPIPCMGR_H_

#include "SpAgentCommon.h"
#include "SpChannel.h"

namespace sp {

	// Forword declarations
	class SpPoint;
	class SpIpcWorker;
	class SpPipeWorker;
	class SpUdpWorker;
	class SpTcpWorker;

// ----------------------------------------------------------------------------- 
// IPC Manager, which is an all-in-one manager for inter-process support.
// -----------------------------------------------------------------------------

	class SpIpcMgr {
  public:
    SpIpcMgr();
    ~SpIpcMgr();

    // Get channel from fd
    // If channel doesn't exist, construct one
    // Return NULL if failed to create one channel
    SpChannel* get_channel(int fd, ChannelRW rw);

		// Get a worker according to the file descriptor.
		// Return the worker if the file descriptor is for supported IPC; otherwise,
		// return NULL.
    SpIpcWorker* get_worker(int fd);

		// Get parameters from "write" functions.
		// Input Param : pt -- the call point from which we get the function
		// Output Param: fd_out -- file descriptor, if it is NULL, then skip it
		// Output Param: buf_out -- the buffer to write, if NULL, then skip it
		// Output Param: c_out -- the character to write, if NULL, then skip it
		// Output Param: size_out -- the size of the buffer, if NULL, then skip it
    void get_write_param(SpPoint* pt, int* fd_out, void** buf_out,
                         char* c_out, size_t* size_out);

		// Get parameters from "read" functions.
		// Input Param : pt -- the call point from which we get the function
		// Output Param: fd_out -- file descriptor, if it is NULL, then skip it
		// Output Param: buf_out -- the buffer to write, if NULL, then skip it
		// Output Param: size_out -- the size of the buffer, if NULL, then skip it
    void get_read_param(SpPoint* pt, int* fd_out, void** buf_out,
                        size_t* size_out);

		// See if the file descriptor is for pipe
    bool is_pipe(int fd);

		// See if the file descriptor is for tcp
    bool is_tcp(int fd);

		// See if the file descriptor is for udp
    bool is_udp(int fd);

		// See if the file descriptor is for any ipc mechanism
    bool is_ipc(int fd);

		// See if the function is a fork
    bool is_fork(const char* f);

		// See if the function is a popen
    bool is_popen(const char* f);

		// See if current process is allowed to execute the payload code
		// This is used in the user-defined payload function.
		// Return 1 if it is allowed to execute payload code (for tracing); otherwise,
		// 0 is returned.
    char start_tracing();

		// Payload functions wrappers, which will be called before user-specified
		// entry-payload function.
    static bool before_entry(SpPoint*);

		// Payload functions wrappers, which will be called before user-specified
		// exit-payload function.
    static bool before_exit(SpPoint*);

		// Get workers
    SpPipeWorker* pipe_worker() const { return pipe_worker_; }
    SpTcpWorker* tcp_worker() const { return tcp_worker_; }
    SpUdpWorker* udp_worker() const { return udp_worker_; }

  protected:

    // IPC workers
    SpPipeWorker* pipe_worker_;
    SpTcpWorker* tcp_worker_;
    SpUdpWorker* udp_worker_;

		// For the ease of iterating all workers
    typedef std::set<SpIpcWorker*> WorkerSet;
    WorkerSet worker_set_;
	};

// ----------------------------------------------------------------------------- 
// The base class for various IPC workers. Possible IPC workers include:
// - TCP worker
// - UDP worker
// - Pipe worker
// -----------------------------------------------------------------------------
	class SpIpcWorker {

  public:
    // An entry_payload function does two things:
    // 1. tracing: user-defined logic
    // 2. propagation: propagate instrumentation
    // Here we let user determine if it is okay to trace
    virtual void set_start_tracing(char yes_or_no, pid_t pid) = 0;

		// Query if it's okay to trace
    virtual char start_tracing() = 0;

		// Inject the agent shared library to the other end of a channel
    virtual bool inject(SpChannel*) = 0;

    // Get IPC channel from a file descriptor.
    // Input Param: fd -- the file descriptor
    // Input Param: rw -- specify whether the channel is read-only or write-only
    // Return NULL if not a valid IPC channel; otherwise, the channel.
    virtual SpChannel* get_channel(int fd, ChannelRW rw);

  protected:
    // fd-to-SpChannel mapping
    typedef std::map<long, SpChannel*> ChannelMap;
    ChannelMap channel_map_write_;
    ChannelMap channel_map_read_;

		// Get inode number from the file descriptor fd
    long get_inode_from_fd(int fd);

		// Create a channel.
    // Assumption: the channel for this fd has not yet existed
		virtual SpChannel* create_channel(int fd, ChannelRW rw) = 0;
	};

// ----------------------------------------------------------------------------- 
// Pipe worker
// -----------------------------------------------------------------------------

	class SpPipeWorker : public SpIpcWorker {
  public:
    SpPipeWorker();
    ~SpPipeWorker();

    virtual void set_start_tracing(char yes_or_no, pid_t);
    virtual char start_tracing();
    virtual bool inject(SpChannel*);

  protected:

    // Child process set
    typedef std::set<pid_t> PidSet;
    PidSet child_proc_set_;

    // Can payload do trace?
    // This buffer is in shared memory
    char* start_tracing_;

	  // Initialize shared memory
    void tracing_internal(char** start_tracing);

		// Is this process using this inode?
    int pid_uses_inode(int pid, int inode);

		// Get all pids that are using this fd
    void get_pids_from_fd(int fd, PidSet& pid_set);

		virtual SpChannel* create_channel(int fd, ChannelRW rw);
	};


// ----------------------------------------------------------------------------- 
// TCP worker
// -----------------------------------------------------------------------------

	class SpTcpWorker : public SpIpcWorker {
  public:
    virtual void set_start_tracing(char yes_or_no, pid_t);
    virtual char start_tracing();
    virtual bool inject(SpChannel*);

  protected:
		virtual SpChannel* create_channel(int fd, ChannelRW rw);
	};

// ----------------------------------------------------------------------------- 
// UDP worker
// -----------------------------------------------------------------------------

	class SpUdpWorker : public SpIpcWorker {
  public:
    virtual void set_start_tracing(char yes_or_no, pid_t);
    virtual char start_tracing();
    virtual bool inject(SpChannel*);

  protected:
		virtual SpChannel* create_channel(int fd, ChannelRW rw);
	};

}

#endif /* _SPIPCMGR_H_ */
