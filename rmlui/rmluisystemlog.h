#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Core.h>

#include "log/loghandler.h"

class RmlSystem : public Rml::SystemInterface
{
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& msg) override
    {
        switch (type)
        {
            case Rml::Log::LT_ALWAYS:
            case Rml::Log::LT_INFO:
            case Rml::Log::LT_DEBUG:
                LOG_INFO("RmlUi: {}", msg.c_str());
                break;

            case Rml::Log::LT_WARNING:
                LOG_WARNING("RmlUi: {}", msg.c_str());
                break;

            case Rml::Log::LT_ERROR:
                LOG_ERROR("RmlUi: {}", msg.c_str());
                break;
            case Rml::Log::LT_ASSERT:
                LOG_CRITICAL("RmlUi: {}", msg.c_str());
                break;
            default:
                LOG_INFO("RmlUi: {}", msg.c_str());
                break;
        }

        return true; // continue execution
    }
};