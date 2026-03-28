#ifndef ENV_HPP
#define ENV_HPP

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <list>

struct attotime {
  static long int from_usec(int usec) { return usec; }
  static long int from_msec(int msec) { return msec * 1000; }
};

struct Timer;

struct Clock {
  long int now = 0;
  std::list<Timer *>timers;

  void advance(long int usec);
  void reset(long int now);
  bool has_events();
  long int next_event();
};

extern Clock tc;

struct Timer {
  long int scheduled_at;
  std::function<void(void)> m_callback;

  void adjust(long int usec) {
    scheduled_at = tc.now + usec;
    enable(true);
  }

  void enable(bool enabled) {
    tc.timers.remove(this);
    if (enabled)
      tc.timers.push_back(this);
  }
};

inline bool Clock::has_events() { return !timers.empty(); }
inline long int Clock::next_event() { 
  auto i = std::min_element(timers.begin(), timers.end(), 
      [](const Timer *a, const Timer *b) {return a->scheduled_at < b->scheduled_at;});
  return (*i)->scheduled_at;
}

inline void Clock::advance(long int usec) {
  long int target = now + usec;
  long int next;

  while (has_events() && (next = next_event()) <= target) {
    for (auto i = timers.begin(); i != timers.end();) {
      if ((*i)->scheduled_at <= next) {
        (*i)->m_callback();
        i = timers.erase(i);
      } else {
        i++;
      }
    }
  }
  now = target;
}

inline void Clock::reset(long int now) {
  this->now = now;
}

Timer *timer_alloc(std::function<void(void)> callback) {
  Timer *t = new Timer;
  t->m_callback = callback;
  return t;
}

#endif // ENV_HPP
