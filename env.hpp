#ifndef ENV_HPP
#define ENV_HPP

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <list>
#include <set>

struct attotime {
	static uint64_t from_usec(int usec) { return usec; }
	static uint64_t from_msec(int msec) { return msec * 1000; }
	const static uint64_t never = (1UL << 63); // _lots_ ot microseconds.
};

struct emu_timer;

struct Clock {
	uint64_t now = 0;
	std::list<emu_timer *>timers;

	void advance(uint64_t usec);
	void reset(uint64_t now);
	bool has_events();
	uint64_t next_event();
};

extern Clock global_clock;
extern std::set<emu_timer *>global_timers;

struct emu_timer {
	uint64_t scheduled_at;
	std::function<void(void)> callback;

	emu_timer(Clock &clock, std::function<void(void)> callback) 
	: scheduled_at(clock.now - 1)
	, callback(callback) {
	} 

	void adjust(uint64_t usec) {
		scheduled_at = usec == attotime::never ? attotime::never : global_clock.now + usec;
		enable(true);
	}

	void enable(bool enabled) {
		global_clock.timers.remove(this);
		if (enabled)
			global_clock.timers.push_back(this);
	}
};

inline emu_timer *timer_alloc(std::function<void(void)> callback) {
	auto timer = new emu_timer(global_clock, callback);
	global_timers.insert(timer);
	return timer;
}

inline void cleanup_global_timers() {
	while (!global_timers.empty()) {
		auto pt = global_timers.begin();
		global_timers.erase(pt);
		auto timer = *pt;
		global_clock.timers.remove(timer);
		delete timer;
	}
}

inline bool Clock::has_events() { return !timers.empty(); }
inline uint64_t Clock::next_event() { 
	auto i = std::min_element(timers.begin(), timers.end(), 
			[](const emu_timer *a, const emu_timer *b) { return a->scheduled_at < b->scheduled_at; });
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
	cleanup_global_timers();
	timers.clear();

	this->now = now;
}
 
class device_t {
public:
	void start() { device_start(); m_started = true; }
	void reset() { device_reset(); m_started = false; }

	virtual void device_start() = 0;
	virtual void device_reset() = 0;

	void fatalerror(const std::string &s) {
		INFO(s);
		REQUIRE(false);
	}

	bool started() { return m_started; }

	bool m_started = false;
};

class device_nvram_interface {
protected:
	// derived class overrides
	virtual void nvram_default() = 0;
	virtual bool nvram_read(std::istream &file) = 0;
	virtual bool nvram_write(std::ostream &file) = 0;
};

#endif // ENV_HPP
