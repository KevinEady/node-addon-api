# ThreadSafeFunction

JavaScript functions can normally only be called from a native addon's main thread. If an addon creates additional threads, then node-addon-api functions that require a `Env`, `Value`, or `Ref` must not be called from those threads.

When an addon has additional threads and JavaScript functions need to be invoked based on the processing completed by those threads, those threads must communicate with the addon's main thread so that the main thread can invoke the JavaScript function on their behalf. The thread-safe function APIs provide an easy way to do this.

These APIs provide the type `Napi::ThreadSafeFunction` as well as APIs to create, destroy, and call objects of this type. `Napi::ThreadSafeFunction::New()` creates a persistent reference that holds a JavaScript function which can be called from multiple threads. The calls happen asynchronously. This means that values with which the JavaScript callback is to be called will be placed in a queue, and, for each value in the queue, a call will eventually be made to the JavaScript function.

`ThreadSafeFunction` objects are destroyed when every thread which uses the object has called `Release()` or has received a return status of `CLOSING` in response to a call to `BlockingCall()` or `NonBlockingCall()`. The queue is emptied before the `ThreadSafeFunction` is destroyed. It is important that `Release()` be the last API call made in conjunction with a given `ThreadSafeFunction`, because after the call completes, there is no guarantee that the `ThreadSafeFunction` is still allocated. For the same reason it is also important that no more use be made of a thread-safe function after receiving a return value of `CLOSING` in response to a call to `BlockingCall()` or `NonBlockingCall()`. Data associated with the `ThreadSafeFunction` can be freed in its `Finalizer` callback which was passed to `ThreadSafeFunction::New()`.

Once the number of threads making use of a `ThreadSafeFunction` reaches zero, no further threads can start making use of it by calling `Acquire()`. In fact, all subsequent API calls associated with it, except `Release()`, will return an error value of napi_closing.

## Example

```cpp
#include <thread>
#include <chrono>
#include <napi.h>

#include <chrono>
#include <napi.h>
#include <thread>

using namespace Napi;

Value Start( const CallbackInfo& info )
{
  Napi::Env env = info.Env();

  if ( info.Length() < 2 )
  {
    throw TypeError::New( env, "Expected two arguments" );
  }
  else if ( !info[0].IsFunction() )
  {
    throw TypeError::New( env, "Expected first arg to be function" );
  }
  else if ( !info[1].IsNumber() )
  {
    throw TypeError::New( env, "Expected second arg to be number" );
  }

  int count = info[1].As<Number>().Int32Value();

  auto tsfn = std::make_shared<ThreadSafeFunction>( ThreadSafeFunction::New(
      env,
      info[0].As<Function>(),  // JavaScript function called asynchronously
      Object(),                // Receiver
      "Resource Name",         // Name
      1,                       // Queue size of 1
      1,                       // Only one thread will use this initially
      (void*)nullptr,          // No finalize data
      []( Napi::Env env, void*, void* ) {}, // Finalizer
      (void*)nullptr  // No context
      ) );

  // Create a new thread
  std::thread nativeThread( [count, tsfn] {

    // Transform native data into JS data
    auto callback = []( Napi::Env env, Function jsCallback, int* value ) {
      jsCallback.Call( {Number::New( env, *value )} );
      // We're finished with it.
      delete value;
    };

    for ( int i = 0; i < count; i++ )
    {
      // Create new data
      int* value = new int( clock() );

      ThreadSafeFunction::Status status = tsfn->BlockingCall( value, callback );
      if ( status != ThreadSafeFunction::Status::OK )
      {
        // Handle error
        break;
      }

      std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    }

    // Release the thread-safe function
    tsfn->Release();
  } );

  nativeThread.detach();

  return env.Undefined();
}

Napi::Object Init( Napi::Env env, Object exports )
{
  exports.Set( "start", Function::New( env, Start ) );
  return exports;
}

NODE_API_MODULE( clock, Init )
```

The above code can be used from JavaScript as follows:

```js
const { start } = require('bindings')('clock');

console.log(start(function () {
    console.log("JavaScript callback called with arguments", Array.from(arguments));
} , 5));
```

## Methods

### Constructor

Creates a new empty instance of `Napi::ThreadSafeFunction`.

```cpp
Napi::Function::ThreadSafeFunction();
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object.

```cpp
New(napi_env env,
    const Function& callback,
    const Object& resource,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount,
    DataType* data,
    Finalizer finalizeCallback,
    Context* context);
```

- `[in] env`: The `napi_env` environment in which to construct the `Napi::ThreadSafeFunction` object.
- `[in] callback`: The `Function` to call from another thread.
- `[in] resource`: An object associated with the async work that will be passed to possible async_hooks init hooks.
- `[in] resourceName`: A JavaScript string to provide an identifier for the kind of resource that is being provided for diagnostic information exposed by the async_hooks API.
- `[in] maxQueueSize`: Maximum size of the queue. `0` for no limit.
- `[in] initialThreadCount`: The initial number of threads, including the main thread, which will be making use of this function.
- `[in] data`: Data to be passed to `finalizeCallback`.
- `[in] finalizeCallback`: Function to call when the `ThreadSafeFunction` is being destroyed.  This callback will be invoked on the main thread when the thread-safe function is about to be destroyed. It receives the context and the finalize data given during construction, and provides an opportunity for cleaning up after the threads e.g. by calling `uv_thread_join()`. It is important that, aside from the main loop thread, there be no threads left using the thread-safe function after the finalize callback completes. Must implement `void operator()(Env env, DataType* data, Context* hint)`.
- `[in] context`: Data to attach to the resulting `ThreadSafeFunction`.

Returns a non-empty `Napi::ThreadSafeFunction` instance.


There are several overloaded implementations of `Napi::ThreadSafeFunction::New` for use with optional parameters.


### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with an empty resource object, default finalize callback, and no context.

```cpp
New(napi_env env,
    const Function& callback,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with a default finalize callback and no context.

```cpp
New(napi_env env,
    const Function& callback,
    const Object& resource,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with no context.

```cpp
New(napi_env env,
    const Function& callback,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount,
    DataType* data,
    Finalizer finalizeCallback);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with an empty resource object and default finalize callback.

```cpp
New(napi_env env,
    const Function& callback,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount,
    Context* context);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with an empty resource object.

```cpp
New(napi_env env,
    const Function& callback,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount,
    DataType* data,
    Finalizer finalizeCallback,
    Context* context);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with no context.

```cpp
New(napi_env env,
    const Function& callback,
    const Object& resource,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount,
    DataType* data,
    Finalizer finalizeCallback);
```

### New

Creates a new instance of the `Napi::ThreadSafeFunction` object with an empty resource object, default finalize callback with no data, and no context.

```cpp
New(napi_env env,
    const Function& callback,
    const Object& resource,
    ResourceString resourceName,
    size_t maxQueueSize,
    size_t initialThreadCount
    Context* context);
```

### Acquire

Add a thread to this thread-safe function object, indicating that a new thread will start making use of the thread-safe function. 

```cpp
bool Napi::ThreadSafeFunction::Acquire()
```

Returns `true` if the thread can successfully be added to the thread count.

### Release

Indicate that an existing thread will stop making use of the thread-safe function. A thread should call this API when it stops making use of this thread-safe function. Using any thread-safe APIs after having called this API has undefined results in the current thread, as it may have been destroyed.

```cpp
bool Napi::ThreadSafeFunction::Release()
```

Returns `true` if the thread-safe function has successfully released.

### Abort

"Abort" the thread-safe function. This will cause all subsequent APIs associated with the thread-safe function except `Release()` to return `CLOSING` even before its reference count reaches zero. In particular, `BlockingCall` and `NonBlockingCall()` will return `CLOSING`, thus informing the threads that it is no longer possible to make asynchronous calls to the thread-safe function. This can be used as a criterion for terminating the thread. Upon receiving a return value of `CLOSING` from a threadsafe function call a thread must make no further use of the thread-safe function because it is no longer guaranteed to be allocated.

```cpp
bool Napi::ThreadSafeFunction::Abort()
```

Returns `true` if the thread-safe function has successfully aborted.

### IsAborted

```cpp
bool Napi::ThreadSafeFunction::IsAborted()
```

Returns `true` if the thread-safe function is aborted.

### BlockingCall / NonBlockingCall

Calls the Javascript function in a either a blocking or non-blocking fashion.
- `BlockingCall()`: the API blocks until space becomes available in the queue. Will never block if the thread-safe function was created with a maximum queue size of `0`.
- `NonBlockingCall()`: will return `ThreadSafeFunction::Status::FULL` if the queue was full, preventing data from being successfully added to the queue

```cpp
ThreadSafeFunction::Status Napi::ThreadSafeFunction::BlockingCall(DataType* data, Callback callback) const

ThreadSafeFunction::Status Napi::ThreadSafeFunction::NonBlockingCall(DataType* data, Callback callback) const
```

- `[in] data`: Data to pass to `callback`
- `[in] callback`: C++ function that is invoked on the main thread. The callback receives the `ThreadSafeFunction`'s JavaScript callback function to call as an `Napi::Function` in its parameters ~~, as well as the `Context*` context pointer used when creating the `Napi::ThreadSafeFunction`,~~ and the `DataType*` data pointer. Must implement `void operator()(napi_env env, Function jsCallback, DataType* data)`. It is not necessary to call into JavaScript via `MakeCallback()` because N-API runs `callback` in a context appropriate for callbacks.

Will return one of `ThreadSafeFunction::Status`:
- `OK`: Call was successfully added to the queue
- `CLOSE`: The thread-safe function is closed and cannot accept more calls.
- `FULL`: The queue was full when trying to call in a non-blocking method.
- `ERROR`: An error adding to the queue

There are several overloaded implementations of `BlockingCall()` and `NonBlockingCall()` for use with optional parameters.

### BlockingCall / NonBlockingCall

Calls the Javascript function in a blocking or non-blocking fashion, with no data passed to `Callback`.

```cpp
ThreadSafeFunction::Status Napi::ThreadSafeFunction::BlockingCall(Callback callback) const
ThreadSafeFunction::Status Napi::ThreadSafeFunction::NonBlockingCall(Callback callback) const
```

### BlockingCall / NonBlockingCall

Calls the `ThreadSafeFunction`'s JavaScript callback with no arguments.

```cpp
ThreadSafeFunction::Status Napi::ThreadSafeFunction::BlockingCall() const
ThreadSafeFunction::Status Napi::ThreadSafeFunction::NonBlockingCall() const
```
