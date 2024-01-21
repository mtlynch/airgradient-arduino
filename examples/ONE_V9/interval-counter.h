#ifndef EXAMPLES_ONE_V9_INTERVAL_COUNTER_H
#define EXAMPLES_ONE_V9_INTERVAL_COUNTER_H

typedef unsigned long timestamp_t;

class IntervalCounter {
  public:
    IntervalCounter(timestamp_t intervalInMilliseconds) :
      intervalInMilliseconds_(intervalInMilliseconds),
      lastFired_(0) {}

    bool IsTimeToFire(timestamp_t currentTimeInMilliseconds) {
      const timestamp_t millisecondsSinceLastFire = currentTimeInMilliseconds - lastFired_;
      if (millisecondsSinceLastFire >= intervalInMilliseconds_) {
        lastFired_ = currentTimeInMilliseconds;
        return true;
      }
      return false;
    }
 private:
    timestamp_t intervalInMilliseconds_;
    timestamp_t lastFired_;
};

#endif EXAMPLES_ONE_V9_INTERVAL_COUNTER_H
