#include <pico/mutex.h>

namespace adsbee::hal {
struct Mutex {
   public:
    Mutex() { mutex_init(&mtx); }
    void lock() { mutex_enter_blocking(&mtx); }
    void unlock() { mutex_exit(&mtx); }

   private:
    mutex_t mtx;
};
}  // namespace adsbee::hal