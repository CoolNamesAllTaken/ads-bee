#pragma once

#include <memory>

#include "ITimeSource.h"
#include "mutex.h"

namespace adsbee::time {

class SysTickTimeSource : ITimeSource {
   public:
    ~SysTickTimeSource();
    uint64_t getTickCount();
    CLOCK_FREQ_E getFrequency() { return CLOCK_FREQ; }

   private:
    static void handleIrq();

    bool _external;
    const uint32_t CLOCK_RESET_VAL;
    const CLOCK_FREQ_E CLOCK_FREQ;
    // We can make this static since there can only be one instance of the class
    static volatile uint64_t _interruptCount;
    static adsbee::hal::Mutex _mutex;

    SysTickTimeSource(bool external);
    SysTickTimeSource(const SysTickTimeSource&) = delete;
    SysTickTimeSource& operator=(const SysTickTimeSource&) = delete;
    SysTickTimeSource(SysTickTimeSource&&) = delete;
    SysTickTimeSource& operator=(SysTickTimeSource&&) = delete;
    friend class SysTimeTimeSourceFactory;
};

class SysTimeTimeSourceFactory {
   public:
    static std::shared_ptr<SysTickTimeSource> create(bool external) {
        if (created) {
            if (external == isExternal) {
                return instance;
            } else {
                return nullptr;
            }
        } else {
            instance = std::shared_ptr<SysTickTimeSource>(new SysTickTimeSource(external));
            isExternal = external;
            return instance;
        }
    }

   private:
    static bool created;
    static bool isExternal;
    static std::shared_ptr<SysTickTimeSource> instance;
};

}  // namespace adsbee::time