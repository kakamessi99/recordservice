// Copyright 2012 Cloudera Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//
// This file contains the main() function for the recordserviced daemon process.
// This daemon can run as either the worker or planner (or both).

#include <unistd.h>
#include <jni.h>

#include "common/logging.h"
#include "common/init.h"
#include "exec/hbase-table-scanner.h"
#include "exec/hbase-table-writer.h"
#include "runtime/hbase-table-factory.h"
#include "codegen/llvm-codegen.h"
#include "common/status.h"
#include "runtime/coordinator.h"
#include "runtime/exec-env.h"
#include "util/jni-util.h"
#include "util/network-util.h"
#include "rpc/thrift-util.h"
#include "rpc/thrift-server.h"
#include "rpc/rpc-trace.h"
#include "service/impala-server.h"
#include "service/fe-support.h"
#include "gen-cpp/ImpalaService.h"
#include "gen-cpp/ImpalaInternalService.h"
#include "util/impalad-metrics.h"
#include "util/thread.h"

using namespace boost;
using namespace impala;
using namespace std;

DECLARE_int32(recordservice_planner_port);
DECLARE_int32(recordservice_worker_port);

int main(int argc, char** argv) {
  InitCommonRuntime(argc, argv, true);

  LlvmCodeGen::InitializeLlvm();
  JniUtil::InitLibhdfs();
  EXIT_IF_ERROR(HBaseTableScanner::Init());
  EXIT_IF_ERROR(HBaseTableFactory::Init());
  EXIT_IF_ERROR(HBaseTableWriter::InitJNI());
  InitFeSupport();

  ExecEnv exec_env(true, true);
  StartThreadInstrumentation(exec_env.metrics(), exec_env.webserver());
  InitRpcEventTracing(exec_env.webserver());

  ThriftServer* recordservice_planner = NULL;
  ThriftServer* recordservice_worker = NULL;

  shared_ptr<ImpalaServer> server;
  EXIT_IF_ERROR(CreateImpalaServer(&exec_env, 0, 0, 0,
      NULL, NULL, NULL, &server));

  EXIT_IF_ERROR(ImpalaServer::StartRecordServiceServices(&exec_env, server,
      FLAGS_recordservice_planner_port, FLAGS_recordservice_worker_port,
      &recordservice_planner, &recordservice_worker));

  Status status = exec_env.StartServices();
  if (!status.ok()) {
    LOG(ERROR) << "recordserviced did not start correctly, exiting. Error: "
               << status.GetDetail();
    ShutdownLogging();
    exit(1);
  }

  EXIT_IF_ERROR(recordservice_planner->Start());
  EXIT_IF_ERROR(recordservice_worker->Start());

  ImpaladMetrics::IMPALA_SERVER_READY->set_value(true);
  LOG(INFO) << "recordserviced has started.";

  recordservice_planner->Join();
  recordservice_worker->Join();
}