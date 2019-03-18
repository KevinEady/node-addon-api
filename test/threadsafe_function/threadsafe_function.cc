#include <uv.h>
#include "napi.h"

using namespace Napi;

constexpr size_t ARRAY_LENGTH = 10;
constexpr size_t MAX_QUEUE_SIZE = 2;

static uv_thread_t uvThreads[2];
struct ThreadSafeFunctionInfo {
  enum CallType {
    DEFAULT,
    BLOCKING,
    NON_BLOCKING
  } type;
  bool abort;
  bool startSecondary;
  FunctionReference jsFinalizeCallback;
  uint32_t maxQueueSize;
} tsfnInfo;

using TSFN = ThreadSafeFunction<ThreadSafeFunctionInfo, uv_thread_t>;

static TSFN tsfn;

// Thread data to transmit to JS
static int ints[ARRAY_LENGTH];

static void SecondaryThread(void* data) {
  TSFN* tsFunction = static_cast<TSFN*>(data);

  if (!tsFunction->Release()) {
    Error::Fatal("SecondaryThread", "ThreadSafeFunction.Release() failed");
  }
}

// Source thread producing the data
static void DataSourceThread(void* data) {
  TSFN* tsFunction = static_cast<TSFN*>(data);

  // FIXME: The `info` should be come from "GetContext()" method
  ThreadSafeFunctionInfo* info = tsFunction->GetContext();

  if (info->startSecondary) {
    if (!tsFunction->Acquire()) {
      Error::Fatal("DataSourceThread", "ThreadSafeFunction.Acquire() failed");
    }

    if (uv_thread_create(&uvThreads[1], SecondaryThread, tsFunction) != 0) {
      Error::Fatal("DataSourceThread", "Failed to start secondary thread");
    }
  }

  bool queueWasFull = false;
  bool queueWasClosing = false;
  for (int index = ARRAY_LENGTH - 1; index > -1 && !queueWasClosing; index--) {
    ThreadSafeFunctionStatus status = ThreadSafeFunctionStatus::ERROR;
    auto callback = [](Env env, Function jsCallback, int* data, ThreadSafeFunctionInfo* context) {
      jsCallback.Call({ Number::New(env, *data) });
    };

    switch (info->type) {
      case ThreadSafeFunctionInfo::DEFAULT:
        status = tsFunction->BlockingCall();
        break;
      case ThreadSafeFunctionInfo::BLOCKING:
        status = tsFunction->BlockingCall(&ints[index], callback);
        break;
      case ThreadSafeFunctionInfo::NON_BLOCKING:
        status = tsFunction->NonBlockingCall(&ints[index], callback);
        break;
    }

    if (info->maxQueueSize == 0) {
      // Let's make this thread really busy for 200 ms to give the main thread a
      // chance to abort.
      uint64_t start = uv_hrtime();
      for (; uv_hrtime() - start < 200000000;);
    }

    switch (status) {
    case ThreadSafeFunctionStatus::FULL:
      queueWasFull = true;
      index++;
      // fall through

    case ThreadSafeFunctionStatus::OK:
      continue;

    case ThreadSafeFunctionStatus::CLOSE:
      queueWasClosing = true;
      break;

    default:
      Error::Fatal("DataSourceThread", "ThreadSafeFunction.*Call() failed");
    }
  }

  if (info->type == ThreadSafeFunctionInfo::NON_BLOCKING && !queueWasFull) {
    Error::Fatal("DataSourceThread", "Queue was never full");
  }

  if (info->abort && !queueWasClosing) {
    Error::Fatal("DataSourceThread", "Queue was never closing");
  }

  if (!queueWasClosing && !tsFunction->Release()) {
    Error::Fatal("DataSourceThread", "ThreadSafeFunction.Release() failed");
  }
}

static Value StopThread(const CallbackInfo& info) {
  tsfnInfo.jsFinalizeCallback = Napi::Persistent(info[0].As<Function>());
  bool abort = info[1].As<Boolean>();
  if (abort) {
    tsfn.Abort();
  } else {
    tsfn.Release();
  }
  return Value();
}

// Join the thread and inform JS that we're done.
static void JoinTheThreads(Env /* env */,
                           uv_thread_t* theThreads,
                           ThreadSafeFunctionInfo* context) {
  uv_thread_join(&theThreads[0]);
  if (context->startSecondary) {
    uv_thread_join(&theThreads[1]);
  }

  context->jsFinalizeCallback.Call({});
  context->jsFinalizeCallback.Reset();
}

static Value StartThreadInternal(const CallbackInfo& info,
    ThreadSafeFunctionInfo::CallType type) {
  tsfnInfo.type = type;
  tsfnInfo.abort = info[1].As<Boolean>();
  tsfnInfo.startSecondary = info[2].As<Boolean>();
  tsfnInfo.maxQueueSize = info[3].As<Number>().Uint32Value();

  tsfn = TSFN::New(info.Env(), info[0].As<Function>(), Object(),
      "Test", tsfnInfo.maxQueueSize, 2, uvThreads, JoinTheThreads, &tsfnInfo);

  if (uv_thread_create(&uvThreads[0], DataSourceThread, &tsfn) != 0) {
    Error::Fatal("StartThreadInternal", "Failed to start data source thread");
  }

  return Value();
}

static Value Release(const CallbackInfo& /* info */) {
  if (!tsfn.Release()) {
    Error::Fatal("Release", "ThreadSafeFunction.Release() failed");
  }
  return Value();
}

static Value StartThread(const CallbackInfo& info) {
  return StartThreadInternal(info, ThreadSafeFunctionInfo::BLOCKING);
}

static Value StartThreadNonblocking(const CallbackInfo& info) {
  return StartThreadInternal(info, ThreadSafeFunctionInfo::NON_BLOCKING);
}

static Value StartThreadNoNative(const CallbackInfo& info) {
  return StartThreadInternal(info, ThreadSafeFunctionInfo::DEFAULT);
}

Object InitThreadSafeFunction(Env env) {
  for (size_t index = 0; index < ARRAY_LENGTH; index++) {
    ints[index] = index;
  }

  Object exports = Object::New(env);
  exports["ARRAY_LENGTH"] = Number::New(env, ARRAY_LENGTH);
  exports["MAX_QUEUE_SIZE"] = Number::New(env, MAX_QUEUE_SIZE);
  exports["startThread"] = Function::New(env, StartThread);
  exports["startThreadNoNative"] = Function::New(env, StartThreadNoNative);
  exports["startThreadNonblocking"] =
      Function::New(env, StartThreadNonblocking);
  exports["stopThread"] = Function::New(env, StopThread);
  exports["release"] = Function::New(env, Release);

  return exports;
}
