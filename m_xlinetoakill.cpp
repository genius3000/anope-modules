/*
 * IRCd X-Line sync with AKILL
 *
 * (C) 2018 - Matt Schatz (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Syncs X-Lines from the uplink IRCd with the AKILL list.
 *
 * Configuration to put into your modules config:
module { name = "m_xlinetoakill" }
 * Logging is done through the "other" type and category of "akill/sync"
 *
 */

#include "module.h"


static ServiceReference<XLineManager> akills("XLineManager", "xlinemanager/sgline");

class XLineToAkill : public Module
{
	BotInfo *OperServ;

 public:
	XLineToAkill(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, THIRD)
		, OperServ(NULL)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		if (IRCD->GetProtocolName().find("InspIRCd") == std::string::npos)
			throw ModuleException("This module only works with InspIRCd.");

		if (!ModuleManager::FindModule("operserv") || !ModuleManager::FindModule("os_akill"))
			throw ModuleException("This module requires both OperServ and OS_AKILL to function.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.1");
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		OperServ = conf->GetClient("OperServ");
	}

	EventReturn OnMessage(MessageSource &source, Anope::string &command, std::vector<Anope::string> &params)
	{
		if ((command != "ADDLINE" && command != "DELLINE") || params.size() < 2 || !akills)
			return EVENT_CONTINUE;

		// Translate the mask from InspIRCd to Anope format
		const Anope::string linetype = params[0];
		Anope::string mask = params[1];

		// R-Lines are sent as 'n!u@h\sreal\sname' and need to be '/n!u@h#real name/'
		if (linetype == "R")
		{
			size_t space = mask.find("\\s");
			if (space != Anope::string::npos)
			{
				mask = mask.replace(space, 2, "#");
				mask = mask.replace_all_cs("\\s", " ");
			}

			mask = "/" + mask + "/";
		}
		// Z-Lines are sent as 'IP' and need to be '*@IP'
		else if (linetype == "Z")
			mask = "*@" + mask;
		// G-Lines need no translating
		// Ignore any other X-Line types
		else if (linetype != "G")
			return EVENT_CONTINUE;

		// Adding
		if (command == "ADDLINE" && params.size() == 6)
		{
			// Ignore this X-Line if it exists as an AKILL already
			if (akills->HasEntry(mask))
				return EVENT_CONTINUE;

			const Anope::string setby = params[2];
			time_t settime = convertTo<time_t>(params[3]);
			time_t duration = convertTo<time_t>(params[4]);
			const Anope::string reason = params[5];

			time_t expires = (duration == 0) ? duration : settime + duration;

			XLine *x = new XLine(mask, setby, expires, reason, XLineManager::GenerateUID());
			akills->AddXLine(x);

			Log(OperServ, "akill/sync") << "X-Line (" << linetype << ") sync added AKILL on " << mask << " (" << reason << "), expires in " << (expires ? Anope::Duration(expires - settime) : "never") << " [set by " << setby << "]";
		}
		// Removing
		else if (command == "DELLINE")
		{
			// Ignore this X-Line if it doesn't exist as an AKILL
			XLine *x = akills->HasEntry(mask);
			if (!x)
				return EVENT_CONTINUE;

			akills->DelXLine(x);

			Log(OperServ, "akill/sync") << "X-Line (" << linetype << ") sync removed AKILL on " << mask;
		}

		// Standard protocol modules do nothing with ADDLINE and DELLINE,
		// allow other modules to act on these though.
		return EVENT_CONTINUE;
	}
};

MODULE_INIT(XLineToAkill)
