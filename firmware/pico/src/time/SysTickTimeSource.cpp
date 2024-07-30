#include "SysTickTimeSource.h"

#include <hardware/exception.h>
#include <hardware/structs/systick.h>

#include <mutex>

namespace adsbee::time {

std::shared_ptr<SysTickTimeSource> SysTimeTimeSourceFactory::instance = nullptr;
bool SysTimeTimeSourceFactory::created = false;
bool SysTimeTimeSourceFactory::isExternal;
hal::Mutex SysTickTimeSource::_mutex;
volatile uint64_t SysTickTimeSource::_interruptCount;

SysTickTimeSource::SysTickTimeSource(bool external)
    : _external(external),
      CLOCK_RESET_VAL(_external ? 12000000 /* 1 seconds at 12MHz */ : 12500000 /* 0.1 seconds at 125Mhz */),
      CLOCK_FREQ(external ? CLOCK_FREQ_E::FREQ_12_MHZ : CLOCK_FREQ_E::FREQ_125_MHZ) {
    systick_hw->csr = 0 |                      /*enable, false*/
                      (1 << 1) |               /*interrupt enable, true*/
                      (external ? 0 : 1 << 2); /*clock source*/

    systick_hw->rvr = CLOCK_RESET_VAL;
    systick_hw->cvr = 0; /* Clear current value */
    systick_hw->csr = systick_hw->csr | 1;

    exception_set_exclusive_handler(SYSTICK_EXCEPTION, handleIrq);
}

SysTickTimeSource::~SysTickTimeSource() { systick_hw->csr = 0; }

uint64_t SysTickTimeSource::getTickCount() {
    // todo: prevent deadlock with IRQ
    std::lock_guard lock(_mutex);
    return (CLOCK_RESET_VAL * _interruptCount) + (CLOCK_RESET_VAL - systick_hw->cvr);
}

void SysTickTimeSource::handleIrq() {
    // todo: prevent deadlock with getTickCount()
    std::lock_guard lock(_mutex);
    // increment on volatile vars is deprecated
    _interruptCount = _interruptCount + 1;
}

}  // namespace adsbee::time
