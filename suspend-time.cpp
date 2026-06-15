#include <HalonMTA.h>
#include <time.h>
#include <vector>
#include <string>
#include <list>
#include <thread>
#include <unistd.h>
#include <syslog.h>
#include <mutex>
#include <memory>
#include <stdexcept>
#if __cplusplus >= 202002L
#include <chrono>
#endif

struct config
{
	std::shared_ptr<std::string> transportid;
	std::shared_ptr<std::string> localip;
	std::shared_ptr<std::string> remoteip;
	std::shared_ptr<std::string> remotemx;
	std::shared_ptr<std::string> recipientdomain;
	std::shared_ptr<std::string> jobid;
	std::shared_ptr<std::string> grouping;
	std::shared_ptr<std::string> tenantid;
	std::shared_ptr<std::string> tag;
	std::shared_ptr<std::string> id;
	std::list<std::string> _if;
	std::list<std::string> _ifnot;
	std::shared_ptr<std::string> tz;
};

std::mutex configlock;
std::list<struct config> config;
bool stop = false;
std::thread p;

bool isCronNow(const std::string& input, const std::string& tz, time_t now);
bool parseConfig(HalonConfig* cfg, std::list<struct config>& config_);

HALON_EXPORT
int Halon_version()
{
	return HALONMTA_PLUGIN_VERSION;
}

void check_suspend()
{
	time_t n = time(nullptr);
	for (auto & c : config)
	{
		bool match = false;
		if (!c._if.empty())
		{
			match = false;
			for (const auto & i : c._if)
			{
				if (isCronNow(i.c_str(), c.tz && !c.tz->empty() ? *c.tz : "", n))
				{
					match = true;
					break;
				}
			}
		}
		if (!c._ifnot.empty())
		{
			match = true;
			for (const auto & i : c._ifnot)
			{
				if (isCronNow(i.c_str(), c.tz && !c.tz->empty() ? *c.tz : "", n))
				{
					match = false;
					break;
				}
			}
		}
		if (match)
		{
			if (!c.id)
			{
				char* id = HalonMTA_queue_suspend_add3(
					c.transportid.get() ? c.transportid.get()->c_str() : nullptr,
					c.localip.get() ? c.localip.get()->c_str() : nullptr,
					c.remoteip.get() ? c.remoteip.get()->c_str() : nullptr,
					c.remotemx.get() ? c.remotemx.get()->c_str() : nullptr,
					c.recipientdomain.get() ? c.recipientdomain.get()->c_str() : nullptr,
					c.jobid.get() ? c.jobid.get()->c_str() : nullptr,
					c.grouping.get() ? c.grouping.get()->c_str() : nullptr,
					c.tenantid.get() ? c.tenantid.get()->c_str() : nullptr,
					c.tag.get() ? c.tag.get()->c_str() : nullptr, 0);
				c.id.reset(new std::string(id));
				free(id);
				syslog(LOG_INFO, "suspendtime: %s added (transportid=%s, localip=%s, remoteip=%s, remotemx=%s, recipientdomain=%s, jobid=%s, grouping=%s, tenantid=%s, tag=%s)",
					c.id.get()->c_str(),
					c.transportid ? c.transportid.get()->c_str() : "",
					c.localip ? c.localip.get()->c_str() : "",
					c.remoteip ? c.remoteip.get()->c_str() : "",
					c.remotemx ? c.remotemx.get()->c_str() : "",
					c.recipientdomain ? c.recipientdomain.get()->c_str() : "",
					c.jobid ? c.jobid.get()->c_str() : "",
					c.grouping ? c.grouping.get()->c_str() : "",
					c.tenantid ? c.tenantid.get()->c_str() : "",
					c.tag ? c.tag.get()->c_str() : "");
			}
		}
		else
		{
			if (c.id)
			{
				syslog(LOG_INFO, "suspendtime: %s removed", c.id.get()->c_str());
				HalonMTA_queue_suspend_delete(c.id.get()->c_str());
				c.id.reset();
			}
		}
	}
}

HALON_EXPORT
bool Halon_init(HalonInitContext* hic)
{
	HalonConfig* cfg;
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_APPCONFIG, nullptr, 0, &cfg, nullptr);

	if (!parseConfig(cfg, config))
		return false;

	std::lock_guard<std::mutex> lk(configlock);
	check_suspend();
	p = std::thread([] {
		pthread_setname_np(pthread_self(), "p/suspend-time");
		while (true)
		{
			for (size_t i = 0; i < 5 && !stop; ++i)
				sleep(1);
			if (stop)
				break;
			std::lock_guard<std::mutex> lk_(configlock);
			check_suspend();
		}
	});

	return true;
}

HALON_EXPORT
void Halon_config_reload(HalonConfig* hc)
{
	syslog(LOG_INFO, "suspendtime: reloading");

	std::list<struct config> config2;
	if (!parseConfig(hc, config2))
		return;

	std::list<std::shared_ptr<std::string>> remove;
	std::lock_guard<std::mutex> lk(configlock);
	for (auto & c : config)
	{
		bool found = false;
		for (auto & c2 : config2)
		{
#define CMP(k) ((c.k && c2.k && *c.k.get() == *c2.k.get()) || (!c.k && !c2.k))
			if (CMP(transportid) && CMP(localip) && CMP(remoteip) && CMP(remotemx) && CMP(recipientdomain) && CMP(jobid) && CMP(grouping) && CMP(tenantid) && CMP(tz) && CMP(tag))
			{
				c2.id = c.id;
				found = true;
				break;
			}
		}
		if (!found && c.id)
		{
			syslog(LOG_INFO, "adding to remove list");
			remove.push_back(c.id);
		}
	}

	config = config2;
	check_suspend();
	for (auto & id : remove)
	{
		syslog(LOG_INFO, "suspendtime: %s removed (config)", id.get()->c_str());
		HalonMTA_queue_suspend_delete(id.get()->c_str());
	}
}

HALON_EXPORT
void Halon_cleanup()
{
	stop = true;
	p.join();
}

bool parseConfig(HalonConfig* cfg, std::list<struct config>& config_)
{
	auto suspends = HalonMTA_config_object_get(cfg, "suspends");
	if (!suspends)
		return true;

	size_t l = 0;
	HalonConfig* suspend;
	while ((suspend = HalonMTA_config_array_get(suspends, l++)))
	{
		const char* transportid = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "transportid"), nullptr);
		const char* localip = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "localip"), nullptr);
		const char* remoteip = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "remoteip"), nullptr);
		const char* remotemx = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "remotemx"), nullptr);
		const char* recipientdomain = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "recipientdomain"), nullptr);
		const char* jobid = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "jobid"), nullptr);
		const char* grouping = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "grouping"), nullptr);
		const char* tenantid = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "tenantid"), nullptr);
		const char* tag = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "tag"), nullptr);
		const char* tz = HalonMTA_config_string_get(HalonMTA_config_object_get(suspend, "timezone"), nullptr);

		struct config m;
		if (transportid) m.transportid = std::make_shared<std::string>(transportid);
		if (localip) m.localip = std::make_shared<std::string>(localip);
		if (remoteip) m.remoteip = std::make_shared<std::string>(remoteip);
		if (remotemx) m.remotemx = std::make_shared<std::string>(remotemx);
		if (recipientdomain) m.recipientdomain = std::make_shared<std::string>(recipientdomain);
		if (jobid) m.jobid = std::make_shared<std::string>(jobid);
		if (grouping) m.grouping = std::make_shared<std::string>(grouping);
		if (tenantid) m.tenantid = std::make_shared<std::string>(tenantid);
		if (tag) m.tag = std::make_shared<std::string>(tag);
		if (tz) m.tz = std::make_shared<std::string>(tz);

		auto ifs = HalonMTA_config_object_get(suspend, "if");
		if (ifs)
		{
			size_t i = 0;
			HalonConfig* _if;
			while ((_if = HalonMTA_config_array_get(ifs, i++)))
			{
				auto x = HalonMTA_config_string_get(_if, nullptr);
				if (!x)
					return false;
				m._if.push_back(x);
			}
		}
		ifs = HalonMTA_config_object_get(suspend, "ifnot");
		if (ifs)
		{
			size_t i = 0;
			HalonConfig* _if;
			while ((_if = HalonMTA_config_array_get(ifs, i++)))
			{
				auto x = HalonMTA_config_string_get(_if, nullptr);
				if (!x)
					return false;
				m._ifnot.push_back(x);
			}
		}

		config_.push_back(m);
	}
	return true;
}

std::vector<std::string> split(const std::string& str, const std::string& separator)
{
	std::vector<std::string> result;

	size_t pos = std::string::npos, lpos = 0;
	while ((pos = str.find(separator, lpos)) != std::string::npos)
	{
		result.push_back(str.substr(lpos, pos - lpos));
		lpos = pos + separator.size();
	}

	if (pos == std::string::npos)
		result.push_back(str.substr(lpos));
	else
		result.push_back(str.substr(lpos, pos - lpos));

	return result;
}

bool isCronNow(const std::string& input, const std::string& tz, time_t now)
{
	auto inputfields = split(input, " ");
	if (inputfields.size() != 5)
		throw std::runtime_error("Cron consists of more than 5 fields");

	struct tm buf;
	if (!tz.empty())
	{
#if __cplusplus >= 202002L
		auto now_ = std::chrono::system_clock::from_time_t(now);
		auto zt = std::chrono::zoned_seconds(tz, std::chrono::floor<std::chrono::seconds>(now_));
		auto lt = zt.get_local_time();

		auto d = std::chrono::floor<std::chrono::days>(lt);
		std::chrono::year_month_day ymd{d};
		std::chrono::hh_mm_ss hms{lt - d};
		buf.tm_year = int(ymd.year()) - 1900;
		buf.tm_mon  = unsigned(ymd.month()) - 1;
		buf.tm_mday = unsigned(ymd.day());
		buf.tm_hour = int(hms.hours().count());
		buf.tm_min  = int(hms.minutes().count());
		buf.tm_sec  = int(hms.seconds().count());
		buf.tm_wday = std::chrono::weekday{d}.c_encoding();
		auto jan1 = std::chrono::local_days{ymd.year() / std::chrono::January / 1};
		buf.tm_yday = int((d - jan1).count());
		buf.tm_isdst = -1;
#else
		throw std::runtime_error("Timezone support is not available on this platform");
#endif
	}
	else
	{
		if (!localtime_r(&now, &buf))
			throw std::runtime_error("localtime_r failed");
	}

	struct field
	{
		size_t min;
		size_t max;
		size_t cur;
	};
	std::vector<field> fields = {
		{ 0, 59, (size_t)buf.tm_min },
		{ 0, 23, (size_t)buf.tm_hour },
		{ 1, 31, (size_t)buf.tm_mday },
		{ 1, 12, (size_t)buf.tm_mon + 1 },
		{ 0, 6, (size_t)buf.tm_wday },
	};

	for (size_t i = 0; i < 5; ++i)
	{
		bool o = false;
		for (const auto & v : split(inputfields[i], ","))
		{
			size_t step = 0;
			auto s = split(v, "/");
			if (s.size() > 2)
				throw std::runtime_error("invalid step");
			if (s.size() == 2)
			{
				char* end;
				step = strtoul(s[1].c_str(), &end, 10);
				if (end != s[1].c_str() + s[1].size())
					throw std::runtime_error(s[1] + " is invalid number");
			}
			auto r = split(s[0], "-");
			switch (r.size())
			{
				case 1:
				{
					if (r[0] == "*")
					{
						if (!step || fields[i].cur % step == 0)
							o = true;
						break;
					}
					char* end;
					auto n = strtoul(r[0].c_str(), &end, 10);
					if (end != r[0].c_str() + r[0].size())
						throw std::runtime_error(r[0] + " is invalid number");
					if (n < fields[i].min)
						throw std::runtime_error("invalid number too small");
					if (n > fields[i].max)
						throw std::runtime_error("invalid number too range");
					if (step)
					{
						if (n >= fields[i].cur && fields[i].cur % step == 0)
							o = true;
					}
					else
					{
						if (n == fields[i].cur)
							o = true;
					}
				}
				break;
				case 2:
				{
					char* end;
					auto b = strtoul(r[0].c_str(), &end, 10);
					if (end != r[0].c_str() + r[0].size())
						throw std::runtime_error(r[0] + " is invalid number");
					auto e = strtoul(r[1].c_str(), &end, 10);
					if (end != r[1].c_str() + r[1].size())
						throw std::runtime_error(r[1] + " is invalid number");
					if (b > e)
						throw std::runtime_error("invalid range");
					if (b < fields[i].min)
						throw std::runtime_error("invalid range too small");
					if (e > fields[i].max)
						throw std::runtime_error("invalid range too range");
					if (fields[i].cur >= b && fields[i].cur <= e && (!step || fields[i].cur % step == 0))
						o = true;
				}
				break;
				default:
					throw std::runtime_error("invalid range");
				break;
			}
			if (o)
				break;
		}
		if (!o)
			return false;
	}
	return true;
}
