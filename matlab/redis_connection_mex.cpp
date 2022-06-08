#include "mex.h"
#include "class_handle.hpp"
#include "mex_helpers.hpp"
#include <river/river.h>

using namespace river;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // Get the command string
  char cmd[64];
  if (nrhs < 1 || mxGetString(prhs[0], cmd, sizeof(cmd)))
    mexErrMsgTxt("First input should be a command string less than 64 characters long.");

  // New
  if (!strcmp("new", cmd)) {
    if (nlhs != 1) {
      mexErrMsgTxt("New: One output expected.");
    }

    if (nrhs == 3 || nrhs == 4) {
      auto stream_name = to_string(prhs[1]);
      int port = to_int(prhs[2]);
      if (nrhs == 3) {
        plhs[0] = convertPtr2Mat<RedisConnection>(
            new RedisConnection(*stream_name, port));
      } else {
        auto password = to_string(prhs[1]);
        plhs[0] = convertPtr2Mat<RedisConnection>(
            new RedisConnection(
              *stream_name,
              port,
              *password));
      }
    } else{
      mexErrMsgTxt("New: unexpected arguments.");
    }

    return;
  }

  // Check there is a second input, which should be the class instance handle
  if (nrhs < 2)
    mexErrMsgTxt("Second input should be a class instance handle.");

  // Delete
  if (!strcmp("delete", cmd)) {
    // Destroy the C++ object
    destroyObject<RedisConnection>(prhs[1]);
    // Warn if other commands were ignored
    if (nlhs != 0 || nrhs != 2)
      mexWarnMsgTxt("Delete: Unexpected arguments ignored.");
    return;
  }

  // Only properties on this class
  if (nrhs != 2) {
    mexWarnMsgTxt("Unexpected arguments ignored.");
    return;
  }
  if (nlhs == 0) {
    return;
  }
  if (nlhs != 1) {
    mexWarnMsgTxt("Unexpected arguments ignored.");
    return;
  }

  RedisConnection* instance = convertMat2Ptr<RedisConnection>(prhs[1]);
  if (!strcmp("redis_hostname", cmd)) {
    plhs[0] = from_string(instance->redis_hostname_);
  } else if (!strcmp("redis_port", cmd)) {
    plhs[0] = from_int(instance->redis_port_);
  } else if (!strcmp("redis_password", cmd)) {
    plhs[0] = from_string(instance->redis_password_);
  } else {
    mexErrMsgTxt("Command not recognized.");
  }
}
