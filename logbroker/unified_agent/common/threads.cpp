#include "threads.h"

#include <util/system/thread.h>

namespace NUnifiedAgent {
    class TNamedThreadPool::TFactory: public IThreadFactory {
    public:
        explicit TFactory(const TString& name)
            : Name(name)
        {
        }

        inline const TString& GetName() const noexcept {
            return Name;
        }

        auto DoCreate() -> IThread* override {
            return new TNamedPoolThread(Name);
        }

    private:
        class TNamedPoolThread: public IThread {
        public:
            explicit TNamedPoolThread(const TString& name)
                : Thread(nullptr)
                , Name(name)
            {
            }

            ~TNamedPoolThread() override {
                if (Thread) {
                    Thread->Detach();
                }
            }

            void DoRun(IThreadAble* func) override {
                Thread.Reset(new TThread(TThread::TParams(ThreadProc, func).SetName(Name)));
                Thread->Start();
            }

            void DoJoin() noexcept override {
                if (!Thread) {
                    return;
                }

                Thread->Join();
                Thread.Destroy();
            }

        private:
            static void* ThreadProc(void* v) {
                static_cast<IThreadAble*>(v)->Execute();
                return nullptr;
            }

        private:
            THolder<TThread> Thread;
            const TString& Name;
        };

    private:
        TString Name;
    };

    TNamedThreadPool::TNamedThreadPool(const TString& threadName, TScopeLogger& logger)
        : Factory(MakeHolder<TFactory>(threadName))
        , ThreadPool(Factory.Get())
        , StartCount(0)
        , Logger(logger)
    {
    }

    TNamedThreadPool::~TNamedThreadPool() = default;

    bool TNamedThreadPool::Add(IObjectInQueue* obj) {
        return ThreadPool.Add(obj);
    }

    void TNamedThreadPool::Start(size_t threadCount, size_t queueSizeLimit) {
        if (StartCount.fetch_add(1) == 0) {
            ThreadPool.Start(threadCount, queueSizeLimit);
            YLOG_INFO(Sprintf("thread pool [%s] started", Factory->GetName().c_str()));
        }
    }

    void TNamedThreadPool::Stop() noexcept {
        if (StartCount.fetch_sub(1) == 1) {
            ThreadPool.Stop();
            YLOG_INFO(Sprintf("thread pool [%s] stopped", Factory->GetName().c_str()));
        }
    }

    size_t TNamedThreadPool::Size() const noexcept {
        return ThreadPool.Size();
    }
}
