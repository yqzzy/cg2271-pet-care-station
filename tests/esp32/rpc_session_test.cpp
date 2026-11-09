#include "rpc_session.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
  RpcSession session;
  assert(!session.Pending() && !session.InFlight("0001") && !session.Cached("0001"));
  session.Begin("0001", 10);
  assert(session.Pending() && session.InFlight("0001"));
  assert(!session.InFlight("1") && !session.InFlight("0002"));
  assert(!session.Complete(9, true, true));
  assert(session.Pending());
  assert(session.Complete(10, true, true));
  assert(!session.Pending() && session.Cached("0001"));
  assert(strcmp(session.Response(), "true") == 0);
  session.Begin("0002", 11);
  assert(session.InFlight("0002") && session.Cached("0001"));
  assert(strcmp(session.Response(), "true") == 0); // A replay while B is pending.
  assert(session.Complete(11, false, false));
  assert(strcmp(session.Response(), "{\"error\":\"light_command_failed\"}") == 0);
  session.Reset();
  assert(!session.Pending() && !session.Cached("0002"));
  assert(!session.Complete(11, true, false));
  puts("PASS: RPC in-flight/recent duplicates, outcome correlation and session reset");
}
