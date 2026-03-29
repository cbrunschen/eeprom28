#ifndef ENV_HPP
#define ENV_HPP

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <list>

struct attotime {
	static uint64_t from_usec(int usec) { return usec; }
	static uint64_t from_msec(int msec) { return msec * 1000; }
	const static uint64_t never = (1UL << 63); // _lots_ ot microseconds.
};

struct Timer;

struct Clock {
	uint64_t now = 0;
	std::list<Timer *>timers;

	void advance(uint64_t usec);
	void reset(uint64_t now);
	bool has_events();
	uint64_t next_event();
};

extern Clock tc;

struct Timer {
	Clock &clock;
	uint64_t scheduled_at;
	std::function<void(void)> callback;

	Timer(Clock &clock, std::function<void(void)> callback) 
	: clock(clock)
	, scheduled_at(clock.now - 1)
	, callback(callback) {
	} 

	void adjust(uint64_t usec) {
		scheduled_at = usec == attotime::never ? attotime::never : clock.now + usec;
		enable(true);
	}

	void enable(bool enabled) {
		clock.timers.remove(this);
		if (enabled)
			clock.timers.push_back(this);
	}
};

Timer *timer_alloc(std::function<void(void)> callback) {
	return new Timer(tc, callback);
}

void timer_delete(Timer *t) {
	if (t) {
		t->enable(false);
		delete t;
	}
}

inline bool Clock::has_events() { return !timers.empty(); }
inline uint64_t Clock::next_event() { 
	auto i = std::min_element(timers.begin(), timers.end(), 
			[](const Timer *a, const Timer *b) { return a->scheduled_at < b->scheduled_at; });
	return (*i)->scheduled_at;
}

inline void Clock::advance(uint64_t usec) {
	long int target = now + usec;
	long int next;

	while (has_events() && (next = next_event()) <= target) {
		for (auto i = timers.begin(); i != timers.end();) {
			if ((*i)->scheduled_at <= next) {
				(*i)->callback();
				i = timers.erase(i);
			} else {
				i++;
			}
		}
	}
	now = target;
}

inline void Clock::reset(uint64_t now) {
	// remove any previously added timers.
	while (!timers.empty()) {
		Timer *t = *timers.begin();
		timers.pop_front();
		timer_delete(t);
	}

	this->now = now;
}

#endif // ENV_HPP
