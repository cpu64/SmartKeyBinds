#include "bridge.hpp"

#include <KPluginFactory>

namespace KWin
{

class KWIN_EXPORT BridgeFactory : public PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "./metadata.json")
    Q_INTERFACES(KWin::PluginFactory)

  public:
    explicit BridgeFactory() = default;

    std::unique_ptr<Plugin> create() const override
    {
        return std::make_unique<Bridge>();
    }
};

} // namespace KWin

#include "main.moc"
