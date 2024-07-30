#include <memory>
#include <type_traits>

#include "ITimeSource.h"

namespace adsbee::time {
template <typename TimeSource, typename std::enable_if_t<std::is_base_of<ITimeSource, TimeSource>::value, bool> = true>
class MlatTime {
   public:
    MlatTime(std::shared_ptr<TimeSource> timeSource)
        : _timeSource(timeSource), _timeResetTicks(_timeSource->getTickCount()) {}

    uint64_t getCurrentTime() {
        auto currentTicks = _timeSource->getTickCount() - _timeResetTicks;
        return convertTime(_timeSource->getFrequency(), currentTicks) & TIME_BIT_MASK;
    }

    void onPpsPulse() {
        // todo
    }

   private:
    constexpr uint64_t convertTime(CLOCK_FREQ_E frequency, uint64_t ticks) {
        switch (frequency) {
            case CLOCK_FREQ_E::FREQ_125_MHZ:
                return (ticks * 96) / 1000;
            case CLOCK_FREQ_E::FREQ_12_MHZ:
                return ticks;
            default:
                break;
        }
    }
    std::shared_ptr<TimeSource> _timeSource;
    uint64_t _timeResetTicks;
    static constexpr uint8_t TIME_BIT_DEPTH = 48;
    static constexpr uint64_t TIME_BIT_MASK = UINT64_MAX >> 64 - TIME_BIT_DEPTH;
};
}  // namespace adsbee::time