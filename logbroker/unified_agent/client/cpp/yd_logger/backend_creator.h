#include <library/cpp/logger/backend_creator.h>

namespace NUnifiedAgent {
    class TLogYdBackendCreator : public TLogBackendCreatorBase {
    public:
        TLogYdBackendCreator();
        bool Init(const IInitContext& ctx) override;

        static TFactory::TRegistrator<TLogYdBackendCreator> Registrar;

    protected:
        void DoToJson(NJson::TJsonValue& value) const override;

    private:
        THolder<TLogBackend> DoCreateLogBackend() const override;
    };
}
