//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

class ServerContextManager
{
public:
    static void RegisterThreadContext(ServerThreadContext* threadContext);
    static void UnRegisterThreadContext(ServerThreadContext* threadContext);

    static void RegisterScriptContext(ServerScriptContext* scriptContext);
    static void UnRegisterScriptContext(ServerScriptContext* scriptContext);    

    static bool CheckLivenessAndAddref(ServerScriptContext* context);
    static bool CheckLivenessAndAddref(ServerThreadContext* context);

private:
    static JsUtil::BaseHashSet<ServerThreadContext*, HeapAllocator> threadContexts;
    static JsUtil::BaseHashSet<ServerScriptContext*, HeapAllocator> scriptContexts;
    static CriticalSection cs;

#ifdef STACK_BACK_TRACE
public:
    template<class T>
    struct ClosedContextEntry
    {
        __declspec(noinline)
        ClosedContextEntry(T* context)
            :context(context)
        {
            stack = StackBackTrace::Capture(&NoThrowHeapAllocator::Instance, 2);
        }
        ~ClosedContextEntry()
        {
            if (stack)
            {
                stack->Delete(&NoThrowHeapAllocator::Instance);
            }
        }
        T* context;
        union {
            DWORD runtimeProcId;
            ServerThreadContext* threadCtx; 
        };
        StackBackTrace* stack;
    };

    static void RecordCloseContext(ServerThreadContext* context)
    {
        auto record = HeapNewNoThrow(ClosedContextEntry<ServerThreadContext>, context);
        record->runtimeProcId = context->GetRuntimePid();
        ClosedThreadContextList.PrependNoThrow(&HeapAllocator::Instance, record);
    }
    static void RecordCloseContext(ServerScriptContext* context)
    {
        auto record = HeapNewNoThrow(ClosedContextEntry<ServerScriptContext>, context);
        record->threadCtx = context->GetThreadContext();
        ClosedScriptContextList.PrependNoThrow(&HeapAllocator::Instance, record);
    }

    static SList<ClosedContextEntry<ServerThreadContext>*, NoThrowHeapAllocator> ClosedThreadContextList;
    static SList<ClosedContextEntry<ServerScriptContext>*, NoThrowHeapAllocator> ClosedScriptContextList;
#endif

    static void Shutdown()
    {
#ifdef STACK_BACK_TRACE
        while (!ClosedThreadContextList.Empty())
        {
            auto record = ClosedThreadContextList.Pop();
            if (record)
            {
                delete record;
            }
        }
        while (!ClosedScriptContextList.Empty())
        {
            auto record = ClosedScriptContextList.Pop();
            if (record)
            {
                delete record;
            }
        }
#endif
    }
};

struct ContextClosedException {};

template<class T>
struct AutoReleaseContext
{
    AutoReleaseContext(T* context)
        :context(context)
    {
        if (!ServerContextManager::CheckLivenessAndAddref(context))
        {
            // Don't assert here because ThreadContext can be closed before scriptContext closing call
            // and ThreadContext closing causes all related scriptContext be closed
            context = nullptr;
            throw ContextClosedException();
        }
    }

    ~AutoReleaseContext()
    {
        if (context) 
        {
            context->Release();
        }
    }

    T* context;
};

template<typename Fn>
HRESULT ServerCallWrapper(ServerThreadContext* threadContextInfo, Fn fn);
template<typename Fn>
HRESULT ServerCallWrapper(ServerScriptContext* scriptContextInfo, Fn fn);
