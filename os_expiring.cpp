/*
 * OperServ Expiring
 *
 * (C) 2016 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Check registered nick and/or channel list for any soon to be expire
 *
 * Syntax: EXPIRING [NICK|CHAN] [time]
 * Both nick and channels will be listed if not specified
 * Configured defaults will be used if no time is specified
 *
 * Configuration to put into your operserv config:
module { name = "os_expiring" }
command { service = "OperServ"; name = "EXPIRING"; command = "operserv/expiring"; permission = "operserv/expiring"; }
 *
 * Don't forget to add 'operserv/expiring' to your oper permissions
 */

#include "module.h"


class CommandOSExpiring : public Command
{

 private:
	void ProcessNickList(CommandSource &source, Anope::string range)
	{
		unsigned nnicks = 0;
		time_t trange;
		time_t nick_expiry = Config->GetModule("nickserv")->Get<time_t>("expire", "21d");
		time_t nick_uc_expiry = Config->GetModule("ns_register")->Get<time_t>("unconfirmedexpire", "1d");
		unsigned listmax = Config->GetModule("nickserv")->Get<unsigned>("listmax", "50");

		if (range.equals_ci("default"))
		{
			trange = nick_expiry / 4;
			range = Anope::Duration(trange, source.GetAccount());
		}
		else
		{
			trange = Anope::DoTime(range);
			range = Anope::Duration(trange, source.GetAccount());
		}

		float nick_percent = static_cast<float>(trange) / static_cast<float>(nick_expiry) * 100.0;
		if (nick_percent > 90)
			return source.Reply(_("The range of %s is too close to (or greater than) the nick default expiry (%s). Not running a list."), range.c_str(), Anope::Duration(nick_expiry, source.GetAccount()).c_str());
		trange += Anope::CurTime;

		ListFormatter list(source.GetAccount());
		list.AddColumn(_("Nick")).AddColumn(_("Expires"));

		Anope::map<NickAlias *> ordered_map;
		for (nickalias_map::const_iterator it = NickAliasList->begin(), it_end = NickAliasList->end(); it != it_end; ++it)
			ordered_map[it->first] = it->second;

		for (Anope::map<NickAlias *>::const_iterator it = ordered_map.begin(), it_end = ordered_map.end(); it != it_end; ++it)
		{
			const NickAlias *na = it->second;
			time_t this_expires;

			if (na->HasExt("NS_NO_EXPIRE"))
				continue;

			if (na->nc->HasExt("UNCONFIRMED"))
				this_expires = na->last_seen + nick_uc_expiry;
			else
				this_expires = na->last_seen + nick_expiry;

			if (this_expires <= trange)
			{
				if (++nnicks <= listmax)
				{
					ListFormatter::ListEntry entry;
					entry["Nick"] = na->nick;
					Anope::string expires = Anope::strftime(this_expires, source.GetAccount());
					if (na->nc->HasExt("NS_SUSPENDED"))
						entry["Expires"] = expires + Language::Translate(source.GetAccount(), _(" [Suspended]"));
					else if (na->nc->HasExt("UNCONFIRMED"))
						entry["Expires"] = expires + Language::Translate(source.GetAccount(), _(" [Unconfirmed]"));
					else
						entry["Expires"] = expires;
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply(_("No nicks will expire within %s."), range.c_str());
		else
		{
			source.Reply(_("List of nicks expiring within %s:"), range.c_str());

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply(_("End of list - %d/%d matches shown."), nnicks > listmax ? listmax : nnicks, nnicks);
		}
		return;
	}

	void ProcessChanList(CommandSource &source, Anope::string range)
	{
		unsigned nchans = 0;
		time_t trange;
		time_t chan_expiry = Config->GetModule("chanserv")->Get<time_t>("expire", "14d");
		unsigned listmax = Config->GetModule("chanserv")->Get<unsigned>("listmax", "50");

		if (range.equals_ci("default"))
		{
			trange = chan_expiry / 4;
			range = Anope::Duration(trange, source.GetAccount());
		}
		else
		{
			trange = Anope::DoTime(range);
			range = Anope::Duration(trange, source.GetAccount());
		}

		float chan_percent = static_cast<float>(trange) / static_cast<float>(chan_expiry) * 100.0;
		if (chan_percent > 90)
			return source.Reply(_("The range of %s is too close to (or greather than) the channel default expiry (%s). Not running a list."), range.c_str(), Anope::Duration(chan_expiry, source.GetAccount()).c_str());
		trange += Anope::CurTime;

		ListFormatter list(source.GetAccount());
		list.AddColumn(_("Name")).AddColumn(_("Expires"));

		Anope::map<ChannelInfo *> ordered_map;
		for (registered_channel_map::const_iterator it = RegisteredChannelList->begin(), it_end = RegisteredChannelList->end(); it != it_end; ++it)
			ordered_map[it->first] = it->second;

		for (Anope::map<ChannelInfo *>::const_iterator it = ordered_map.begin(), it_end = ordered_map.end(); it != it_end; ++it)
		{
			const ChannelInfo *ci = it->second;
			time_t this_expires;

			if (ci->HasExt("CS_NO_EXPIRE"))
				continue;

			this_expires = ci->last_used + chan_expiry;
			if (this_expires <= trange)
			{
				if (++nchans <= listmax)
				{
					ListFormatter::ListEntry entry;
					entry["Name"] = ci->name;
					Anope::string expires = Anope::strftime(this_expires, source.GetAccount());
					if (ci->HasExt("CS_SUSPENDED"))
						entry["Expires"] = expires + Language::Translate(source.GetAccount(), _(" [Suspended]"));
					else
						entry["Expires"] = expires;
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply(_("No channels will expire within %s."), range.c_str());
		else
		{
			source.Reply(_("List of channels expiring within %s:"), range.c_str());

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply(_("End of list - %d/%d matches shown."), nchans > listmax ? listmax : nchans, nchans);
		}
		return;
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		Anope::string choice, range;

		if (params.size() < 1)
		{
			choice = "nickchan";
			range = "default";
		}
		else if (params.size() == 1)
		{
			if (isdigit(params[0][0]))
			{
				choice = "nickchan";
				range = params[0];
			}
			else if (params[0].equals_ci("nick") || params[0].equals_ci("chan"))
			{
				choice = params[0];
				range = "default";
			}
			else
				return this->OnSyntaxError(source, "");
		}
		else if (params.size() == 2 && (params[0].equals_ci("nick") || params[0].equals_ci("chan")))
		{
			choice = params[0];
			range = params[1];
		}
		else
			return this->OnSyntaxError(source, "");

		// Range is in days if not specified
		if (isdigit(range[range.length() - 1]))
			range += "d";

		if (choice.equals_ci("nickchan") || choice.equals_ci("nick"))
			this->ProcessNickList(source, range);
		if (choice.equals_ci("nickchan") || choice.equals_ci("chan"))
			this->ProcessChanList(source, range);
	}

 public:
	CommandOSExpiring(Module *creator) : Command(creator, "operserv/expiring", 0, 2)
	{
		this->SetDesc(_("Check registered nick and/or channel list for any soon to expire"));
		this->SetSyntax(_("[\037nick\037 | \037chan\037] [\037time\037]"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		return this->DoList(source, params);
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Let's you check the registered nick and/or channel list\n"
						"for any that are expiring in the time range specified.\n"));
		source.Reply(" ");
		source.Reply(_("\002EXPIRING\002 will list both nicks and channels.\n"
						"\002EXPIRING NICK\002 will list just the nicks.\n"
						"\002EXPIRING CHAN\002 will list just the channels.\n"
						"\037time\037 is specified as an integer followed by one of \037d\037\n"
						"(days), \037h\037 (hours), or \037m\037 (minutes). Combinations (such as\n"
						"\0371h30m\037) are not permitted. If a unit specifier is not\n"
						"included, the default is days (so \03730\037 by itself means 30\n"
						"days). If a time range is not given, it will use one quarter\n"
						"of the default expiry time (for either list).\n"));
		return true;
	}
};

class OSExpiring : public Module
{
	CommandOSExpiring commandosexpiring;

 public:
	OSExpiring(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		commandosexpiring(this)
	{
		if(Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.4");
	}
};

MODULE_INIT(OSExpiring)
