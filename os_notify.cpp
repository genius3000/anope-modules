/*
 * OperServ Notify
 *
 * (C) 2017 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Notifies opers via log (intended to log to channel) of events done by users matching
 * a set mask.
 * Masks are the same as AKILL: nick!user@host#real (only needing user@host) and allowing
 * regex matching if enabled.
 *
 * Syntax: NOTIFY {ADD | DEL | LIST | VIEW | CLEAR | SHOW} [+expiry [all] mask [:]reason | mask | entry-num | list]
 *
 * Configuration to put into your operserv config:
module { name = "os_notify" }
command { service = "OperServ"; name = "NOTIFY"; command = "operserv/notify"; permission = "operserv/notify"; }
 *
 * Don't forget to add 'operserv/notify' to your oper permissions
 * Create a channel logging tag similar to:
log { target = "#services-notify"; bot = "OperServ"; other = "notify/..."; }
 * The logging is split into 3 categories:
 * notify/user
 * notify/channel
 * notify/commands
 */

/* TODO/Thoughts
 * - Test, test, and more test!
 * - Improve the logging strings/calls?
 */

#include "module.h"


/* Dataset for each Notify mask (entry) */
struct NotifyEntry : Serializable
{
 public:
	Anope::string mask;	/* Mask to match */
	Anope::string reason;	/* Reason for this Notify */
	std::set<char> flags;	/* Flags of what to track */
	Anope::string creator;	/* Nick of creator */
	time_t created;		/* Time of creation */
	time_t expires;		/* Time of expiry */

	NotifyEntry() : Serializable("NotifyEntry") { }

	~NotifyEntry();

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["mask"] << this->mask;
		data["reason"] << this->reason;
		data["flags"] << Anope::string(this->flags.begin(), this->flags.end());
		data["creator"] << this->creator;
		data["created"] << this->created;
		data["expires"] << this->expires;
	}

	static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

/* Maps to track matched Notify Entries and Users */
typedef std::multimap<NotifyEntry *, User *> PerEntryMap;
typedef std::multimap<User *, NotifyEntry *> PerUserMap;

/* Service to access and modify Notify Entries and currently Matched users */
class NotifyService : public Service
{
 protected:
	Serialize::Checker<std::vector<NotifyEntry *> > notifies;
	PerEntryMap match_entry;	/* Multiple Users mapped to one Notify Entry */
	PerUserMap match_user;		/* Multiple Notify Entires mapped to one User */

 public:
	NotifyService(Module *m) : Service(m, "NotifyService", "notify"), notifies("NotifyEntry") { }

	~NotifyService()
	{
		for (unsigned i = notifies->size(); i > 0; --i)
			delete (*notifies).at(i - 1);

		match_entry.clear();
		match_user.clear();
	}

	void AddNotify(NotifyEntry *ne)
	{
		notifies->push_back(ne);
	}

	void DelNotify(NotifyEntry *ne)
	{
		/* Erase all Map items matching to this Notify Entry */
		match_entry.erase(ne);
		for (PerUserMap::reverse_iterator rit = match_user.rbegin(); rit != match_user.rend(); )
		{
			if (rit->second == ne)
				match_user.erase(--rit.base());
			else
				++rit;
		}

		/* Erase this Notify Entry from the Notify vector */
		std::vector<NotifyEntry *>::iterator it = std::find(notifies->begin(), notifies->end(), ne);
		if (it != notifies->end())
			notifies->erase(it);
	}

	void ClearNotifies()
	{
		for (unsigned i = notifies->size(); i > 0; --i)
			delete (*notifies).at(i - 1);

		match_entry.clear();
		match_user.clear();
	}

	/* Expire a Notify Entry */
	void Expire(NotifyEntry *ne)
	{
		Log(LOG_NORMAL, "expire/notify", Config->GetClient("OperServ")) << "Expiring Notify entry " << ne->mask;
		delete ne;
	}

	/* Return a Notify Entry given an index number */
	NotifyEntry *GetNotify(unsigned i)
	{
		if (i >= notifies->size())
			return NULL;

		NotifyEntry *ne = notifies->at(i);
		if (ne && ne->expires && ne->expires <= Anope::CurTime)
		{
			Expire(ne);
			return NULL;
		}

		return ne;
	}

	/* Return an exact matching Notify Entry given a mask */
	NotifyEntry *FindExactNotify(const Anope::string &mask)
	{
		for (unsigned i = notifies->size(); i > 0; --i)
		{
			NotifyEntry *ne = notifies->at(i - 1);
			if (!ne)
				continue;

			if (ne->expires && ne->expires <= Anope::CurTime)
				Expire(ne);
			else if (ne->mask.equals_ci(mask))
				return ne;
		}

		return NULL;
	}

	/* Return a vector of (wildcard) matching Notify Entries given a mask */
	std::vector<NotifyEntry *> FindNotifies(const Anope::string &mask)
	{
		std::vector<NotifyEntry *> matches;

		for (unsigned i = notifies->size(); i > 0; --i)
		{
			NotifyEntry *ne = notifies->at(i - 1);

			if (!ne)
				continue;

			if (ne->expires && ne->expires <= Anope::CurTime)
				Expire(ne);
			else if (Anope::Match(mask, ne->mask, false, true))
				matches.push_back(ne);
		}

		return matches;
	}

	/* Check if a User matches to a mask */
	bool Check(User *u, const Anope::string &mask)
	{
		/* Regex mask: Matches against u@h and n!u@h#r only */
		if (mask.length() >= 2 && mask[0] == '/' && mask[mask.length() - 1] == '/')
		{
			Anope::string uh = u->GetIdent() + '@' + u->host, nuhr = u->nick + '!' + uh + '#' + u->realname;
			Anope::string stripped_mask = mask.substr(1, mask.length() - 2);
			return (Anope::Match(uh, stripped_mask, false, true) || Anope::Match(nuhr, stripped_mask, false, true));
		}

		/* Use 'modes' Entry to perform matching per item (nick, user, host, real) */
		Entry notify_mask("", mask);
		return notify_mask.Matches(u, true);
	}

	std::vector<NotifyEntry *> &GetNotifies()
	{
		return *notifies;
	}

	unsigned GetNotifiesCount()
	{
		return notifies->size();
	}

	/* Check if a User is already mapped to a specific Notify Entry */
	bool ExistsAlready(User *u, NotifyEntry *ne)
	{
		std::pair<PerUserMap::const_iterator, PerUserMap::const_iterator> itpair = match_user.equal_range(u);
		for (PerUserMap::const_iterator it = itpair.first; it != itpair.second; ++it)
		{
			if (it->second == ne)
				return true;
		}

		return false;
	}

	/* Map a User as matched to a specific Notify Entry */
	void AddMatch(User *u, NotifyEntry *ne)
	{
		match_entry.insert(std::make_pair(ne, u));
		match_user.insert(std::make_pair(u, ne));
	}

	/* Remove a User from the matched Maps */
	void DelMatch(User *u)
	{
		match_user.erase(u);
		for (PerEntryMap::reverse_iterator it = match_entry.rbegin(); it != match_entry.rend(); )
		{
			if (it->second == u)
				match_entry.erase(--it.base());
			else
				++it;
		}
	}

	PerEntryMap &GetEntryMap()
	{
		return match_entry;
	}

	PerUserMap &GetUserMap()
	{
		return match_user;
	}

	/* Check if a User is matched to any Notify Entries already */
	bool IsMatch(User *u)
	{
		return (match_user.count(u) > 0);
	}

	/* Check if a User is matched to a Notify Entry with a specific flag */
	bool HasFlag(User *u, char flag)
	{
		std::pair<PerUserMap::const_iterator, PerUserMap::const_iterator> itpair = match_user.equal_range(u);
		for (PerUserMap::const_iterator it = itpair.first; it != itpair.second; ++it)
		{
			if (it->second->flags.count(flag) > 0)
				return true;
		}

		return false;
	}
};

static ServiceReference<NotifyService> notify_service("NotifyService", "notify");

NotifyEntry::~NotifyEntry()
{
	if (notify_service)
		notify_service->DelNotify(this);
}

Serializable* NotifyEntry::Unserialize(Serializable *obj, Serialize::Data &data)
{
	if (!notify_service)
		return NULL;

	NotifyEntry *ne;
	if (obj)
		ne = anope_dynamic_static_cast<NotifyEntry *>(obj);
	else
		ne = new NotifyEntry();

	Anope::string flags;
	data["mask"] >> ne->mask;
	data["reason"] >> ne->reason;
	data["flags"] >> flags;
	data["creator"] >> ne->creator;
	data["created"] >> ne->created;
	data["expires"] >> ne->expires;
	for (unsigned f = 0; f != flags.length(); ++f)
		ne->flags.insert(flags[f]);

	if (!obj)
		notify_service->AddNotify(ne);

	return ne;
}

/* Handle numbered (list) deletions */
class NotifyDelCallback : public NumberList
{
	CommandSource &source;
	unsigned deleted;
	Command *cmd;

 public:
	NotifyDelCallback(CommandSource &_source, const Anope::string &numlist, Command *c) : NumberList(numlist, true), source(_source), deleted(0), cmd(c) { }

	~NotifyDelCallback()
	{
		if (!deleted)
		{
			source.Reply("No matching entries on the Notify list.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		if (deleted == 1)
			source.Reply("Deleted 1 entry from the Notify list.");
		else
			source.Reply("Deleted %d entries from the Notify list.");
	}

	void HandleNumber(unsigned number) anope_override
	{
		if (!number)
			return;

		NotifyEntry *ne = notify_service->GetNotify(number - 1);
		if (!ne)
			return;

		Log(LOG_ADMIN, source, cmd) << "to remove " << ne->mask << " from the list";

		++deleted;
		DoDel(source, ne);
	}

	static void DoDel(CommandSource &source, NotifyEntry *ne)
	{
		delete ne;
	}
};

class CommandOSNotify : public Command
{
 private:
	ServiceReference<NotifyService> nts;

	void DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		Anope::string expiry, str_flags, mask, reason;

		/* Expecting: ADD +expiry flags|* mask [:]reason
		 * Ex:	ADD +30d cdjp idiot!moron@somewhere.com Annoying spammer
		 * Ex:	ADD +30d * helper!help@*.isp.com#Here to help :Impersonating staff and spamming
		 */

		if (params.size() < 4)
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		expiry = params[1];
		time_t expires = Anope::DoTime(expiry);
		/* Like AKILL, default to days if not specified */
		if (isdigit(expiry[expiry.length() - 1]))
			expires *= 86400;
		if (expires > 0)
			expires += Anope::CurTime;

		/* Acceptable flags are:
		 * c = Connects
		 * d = Disconnects
		 * j = channel Joins
		 * k = channel Kicks
		 * m = channel Modes
		 * n = Nick changes
		 * p = channel Parts
		 * s = Services commands (-SET)
		 * S = Services SET commands
		 * t = Topics
		 * u = Usermodes
		 */
		Anope::string all_flags = "Scdjkmnpstu";
		str_flags = params[2];
		if (str_flags == "*")
			str_flags = all_flags;
		if (str_flags.find_first_not_of(all_flags) != Anope::string::npos)
		{
			source.Reply("Incorrect flags character(s) given.");
			return;
		}
		std::set<char> flags;
		for (unsigned f = 0; f < str_flags.length(); ++f)
			flags.insert(str_flags[f]);

		spacesepstream sep(params[3]);
		sep.GetToken(mask);

		if (sep.StreamEnd())
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		if (mask.find('#') != Anope::string::npos)
		{
			Anope::string remaining = sep.GetRemaining();

			size_t co = remaining[0] == ':' ? 0 : remaining.rfind(" :");
			if (co == Anope::string::npos)
			{
				this->OnSyntaxError(source, "ADD");
				return;
			}

			if (co != 0)
				++co;

			reason = remaining.substr(co + 1);
			mask += " " + remaining.substr(0, co);
			mask.trim();
		}
		else
			reason = sep.GetRemaining();

		if (mask.length() >= 2 && mask[0] == '/' && mask[mask.length() - 1] == '/')
		{
			const Anope::string &regexengine = Config->GetBlock("options")->Get<const Anope::string>("regexengine");

			if (regexengine.empty())
			{
				source.Reply("Regex is disabled.");
				return;
			}

			ServiceReference<RegexProvider> provider("Regex", regexengine);
			if (!provider)
			{
				source.Reply("Unable to find regex engine %s.", regexengine.c_str());
				return;
			}

			try
			{
				Anope::string stripped_mask = mask.substr(1, mask.length() - 2);
				delete provider->Compile(stripped_mask);
			}
			catch (const RegexException &ex)
			{
				source.Reply("%s", ex.GetReason().c_str());
				return;
			}
		}

		/* Make sure the mask is at least user@host and isn't all wildcard */
		if (mask.find_first_not_of("/~@.*?") == Anope::string::npos)
		{
			source.Reply(USERHOST_MASK_TOO_WIDE, mask.c_str());
			return;
		}
		else if (mask.find('@') == Anope::string::npos)
		{
			source.Reply(BAD_USERHOST_MASK);
			return;
		}

		/* Create or modify a Notify Entry
		 * Always delete and create new so it Serializes
		 */
		NotifyEntry *ne = nts->FindExactNotify(mask);
		bool created = true;
		if (ne)
		{
			created = false;
			delete ne;
		}
		ne = new NotifyEntry();

		ne->mask = mask;
		ne->reason = reason;
		ne->flags = flags;
		ne->creator = source.GetNick();
		ne->created = Anope::CurTime;
		ne->expires = expires;
		nts->AddNotify(ne);

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		unsigned matches = 0;
		for (user_map::const_iterator it = UserListByNick.begin(); it != UserListByNick.end(); ++it)
		{
			User *u = it->second;

			if (nts->Check(u, ne->mask))
			{
				nts->AddMatch(u, ne);
				matches++;
			}
		}

		Log(LOG_ADMIN, source, this) << "to " << (created ? "add" : "modify") << " a Notify on " << mask << " for reason: " << reason << " (matches: " << matches << " users)";
		source.Reply("%s a Notify on %s which matched %d user(s).", (created ? "Added" : "Modified"), mask.c_str(), matches);
	}

	void DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &match = params.size() > 1 ? params[1] : "";

		if (match.empty())
		{
			this->OnSyntaxError(source, "DEL");
			return;
		}

		if (nts->GetNotifiesCount() == 0)
		{
			source.Reply("Notify list is empty.");
			return;
		}

		if (isdigit(match[0]) && match.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			NotifyDelCallback list(source, match, this);
			list.Process();
		}
		else
		{
			NotifyEntry *ne = nts->FindExactNotify(match);

			if (!ne)
			{
				source.Reply("\002%s\002 not found on the Notify list.", match.c_str());
				return;
			}

			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			Log(LOG_ADMIN, source, this) << "to remove " << ne->mask << " from the list";
			source.Reply("\002%s\002 deleted from the Notify list.", ne->mask.c_str());
			NotifyDelCallback::DoDel(source, ne);
		}
	}

	void ProcessList(CommandSource &source, const std::vector<Anope::string> &params, ListFormatter &list)
	{
		const Anope::string &match = params.size() > 1 ? params[1] : "";

		if (!match.empty() && isdigit(match[0]) && match.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			class ListCallback : public NumberList
			{
				CommandSource &source;
				ListFormatter &list;

			 public:
				ListCallback(CommandSource &_source, ListFormatter &_list, const Anope::string &numstr) : NumberList(numstr, false), source(_source), list(_list) { }

				void HandleNumber(unsigned number) anope_override
				{
					if (!number)
						return;

					const NotifyEntry *ne = notify_service->GetNotify(number - 1);
					if (!ne)
						return;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
					entry["Mask"] = ne->mask;
					entry["Flags"] = Anope::string(ne->flags.begin(), ne->flags.end());
					entry["Reason"] = ne->reason;
					entry["Created"] = Anope::strftime(ne->created, source.nc, true);
					entry["By"] = ne->creator;
					entry["Expires"] = ne->expires ? Anope::Expires(ne->expires, source.nc) : "Never";
					list.AddEntry(entry);
				}
			} nl_list(source, list, match);
			nl_list.Process();
		}
		else
		{
			for (unsigned i = 0; i < nts->GetNotifiesCount(); ++i)
			{
				const NotifyEntry *ne = nts->GetNotify(i);
				if (!ne)
					continue;

				if (match.empty() || match.equals_ci(ne->mask) || Anope::Match(ne->mask, match, false, true))
				{
					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["Mask"] = ne->mask;
					entry["Flags"] = Anope::string(ne->flags.begin(), ne->flags.end());
					entry["Reason"] = ne->reason;
					entry["Created"] = Anope::strftime(ne->created, source.nc, true);
					entry["By"] = ne->creator;
					entry["Expires"] = ne->expires ? Anope::Expires(ne->expires, source.nc) : "Never";
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply("No matching entries on the Notify list.");
		else
		{
			source.Reply("Current Notify list:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of Notify list.");
		}
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (nts->GetNotifiesCount() == 0)
		{
			source.Reply("Notify list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Mask").AddColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (nts->GetNotifiesCount() == 0)
		{
			source.Reply("Notify list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Mask").AddColumn("Flags").AddColumn("Reason");
		list.AddColumn("Created").AddColumn("By").AddColumn("Expires");

		this->ProcessList(source, params, list);
	}

	void DoClear(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (nts->GetNotifiesCount() == 0)
		{
			source.Reply("Notify list is empty.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		nts->ClearNotifies();

		Log(LOG_ADMIN, source, this) << "to clear the list.";
		source.Reply("Notify list has been cleared.");
	}

	void DoShow(CommandSource &source, const std::vector<Anope::string> &params)
	{
		PerEntryMap &current = nts->GetEntryMap();
		if (current.empty())
		{
			source.Reply("No matching Users are currently online.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Flags/Nick").AddColumn("Mask").AddColumn("Reason/Online Since");

		Anope::string last_mask;
		for (PerEntryMap::const_iterator it = current.begin(); it != current.end(); ++it)
		{
			NotifyEntry *ne = it->first;
			User *u = it->second;
			if (!ne || !u)
				continue;

			if (last_mask != ne->mask)
			{
				ListFormatter::ListEntry entry;
				entry["Flags/Nick"] = Anope::string(ne->flags.begin(), ne->flags.end());
				entry["Mask"] = ne->mask;
				entry["Reason/Online Since"] = ne->reason;
				list.AddEntry(entry);

				last_mask = ne->mask;
			}

			ListFormatter::ListEntry subentry;
			subentry["Flags/Nick"] = u->nick;
			subentry["Mask"] = u->GetIdent() + "@" + u->host + "#" + u->realname;
			subentry["Reason/Online Since"] = Anope::strftime(u->signon, source.nc, true);
			list.AddEntry(subentry);
		}

		if (list.IsEmpty())
			source.Reply("No matching entries currently online.");
		else
		{
			source.Reply("Currently matched online users:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of matched online users.");
		}
	}

	void DoRemove(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (nts->GetNotifiesCount() == 0)
		{
			source.Reply("Notify list is empty.");
			return;
		}

		if (params.size() < 2 || params.size() > 2)
		{
			this->OnSyntaxError(source, "REMOVE");
			return;
		}

		PerUserMap &current = nts->GetUserMap();
		if (current.empty())
		{
			source.Reply("No matching Users are currently online.");
			return;
		}

		User *u = User::Find(params[1], true);
		if (!u)
		{
			source.Reply("No user found by the nick of %s", params[1].c_str());
			return;
		}

		if (!nts->IsMatch(u))
		{
			source.Reply("%s is not currently a matched User.", u->nick.c_str());
			return;
		}

		nts->DelMatch(u);

		Log(LOG_ADMIN, source, this) << "to remove " << u->nick << " from the matched Users list for Notify";
		source.Reply("%s has been removed the matched Users list.", u->nick.c_str());
	}

 public:
	CommandOSNotify(Module *creator) : Command(creator, "operserv/notify", 1, 4), nts("NotifyService", "notify")
	{
		this->SetDesc("Manipulate the Notify (watch) list");
		this->SetSyntax("ADD +\037expiry\037 \037flags\037 \037mask\037 [:]\037reason\037");
		this->SetSyntax("DEL [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("LIST [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("VIEW [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("CLEAR");
		this->SetSyntax("SHOW [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("REMOVE \037nick\037");
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!nts)
			return;

		const Anope::string &subcmd = params[0];

		if (subcmd.equals_ci("ADD"))
			this->DoAdd(source, params);
		else if (subcmd.equals_ci("DEL"))
			this->DoDel(source, params);
		else if (subcmd.equals_ci("LIST"))
			this->DoList(source, params);
		else if (subcmd.equals_ci("VIEW"))
			this->DoView(source, params);
		else if (subcmd.equals_ci("CLEAR"))
			this->DoClear(source, params);
		else if (subcmd.equals_ci("SHOW"))
			this->DoShow(source, params);
		else if (subcmd.equals_ci("REMOVE"))
			this->DoRemove(source, params);
		else
			this->OnSyntaxError(source, "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Notify allows Opers to create a list of masks that Users are matched against.\n"
			     "Matching Users have many of their 'events' tracked and logged\n"
			     "(typically to a log channel) for Opers to monitor.");
		source.Reply(" ");
		source.Reply("The \002ADD\002 command adds the given mask to the Notify list.\n"
			     "Reason \002must\002 be given and the mask should be in the format of\n"
			     "nick!user@host#real name, though all that is required is user@host.\n"
			     "If a real name is specified, the reason must be prepended with a :.\n"
			     "Flags are used to decide what to track, for all use \037*\037.\n"
			     "Acceptable flags are \037c\037 for connects, \037d\037 for disconnects,\n"
			     "\037j\037 for joins, \037k\037 for kicks, \037m\037 for channel modes,\n"
			     "\037n\037 for nick changes, \037p\037 for parts, \037s\037 for most\n"
			     "services commands, \037S\037 for more services commands,\n"
			     "\037t\037 for topics, and \037u\037 for usermodes.\n"
			     "\037expiry\037 is specified as an integer followed by one of \037d\37\n"
			     "(days), \037h\037 (hours), or \037m\037 (minutes). Combinations (such as\n"
			     "\0371h30m\037) are not permitted. If a unit specifier is not included,\n"
			     "the default is days (so \037+30\037 by itself means 30 days).\n"
			     "To add a Notify which does not expire, use \037+0\037.");

		const Anope::string &regexengine = Config->GetBlock("options")->Get<const Anope::string>("regexengine");
		if (!regexengine.empty())
		{
			source.Reply(" ");
			source.Reply("Regex matches are also supported using the %s engine.\n"
				     "Note that this will ONLY match against either \n"
				     "\037user@host\037 or \037nick!user@host#real\037\n"
				     "Enclose your pattern in // if this is desired.", regexengine.c_str());
		}

		source.Reply(" ");
		source.Reply("The \002DEL\002, \002LIST\002, and \002VIEW\002 commands can be used\n"
			     "with no parameters, with a mask to match, an entry number, or a list\n"
			     "of entry numbers (1-5 or 1,3 format).");
		source.Reply(" ");
		source.Reply("The \002CLEAR\002 command clears all entries of the Notify list.");
		source.Reply(" ");
		source.Reply("The \002SHOW\002 command lists Notify masks with currently matched Users\n"
			     "It can accept the same parameters as the \002DEL\002, \002LIST\002,\n"
			     "and \002VIEW\002 commands, including no parameters at all.");
		source.Reply(" ");
		source.Reply("The \002REMOVE\002 command removes a user from the matched Users list.\n"
			     "This can be useful if a user gets matched by a playful/silly nick change\n"
			     "or as a temporary removal of tracking of the user.");

		return true;
	}
};

class OSNotify : public Module
{
	NotifyService notifyService;
	Serialize::Type notifyentry_type;
	CommandOSNotify commandosnotify;

	BotInfo *OperServ;

	const Anope::string BuildNUHR(User *u)
	{
		if (!u)
			return "unknown";

		return Anope::string(u->nick + "!" + u->GetIdent() + "@" + u->host + " (" + u->realname + ")");
	}

 public:
	OSNotify(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		notifyService(this), notifyentry_type("NotifyEntry", NotifyEntry::Unserialize),
		commandosnotify(this), OperServ(NULL)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("0.6.0");
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		OperServ = conf->GetClient("OperServ");
	}

	void OnUplinkSync(Server *) anope_override
	{
		if (notifyService.GetNotifiesCount() == 0)
			return;

		std::vector<NotifyEntry *> notifies = notifyService.GetNotifies();

		unsigned matches = 0;
		for (user_map::const_iterator uit = UserListByNick.begin(); uit != UserListByNick.end(); ++uit)
		{
			User *u = uit->second;
			if (u->server && u->server->IsULined())
				continue;

			bool matched = false;
			for (unsigned i = notifies.size(); i > 0; --i)
			{
				NotifyEntry *ne = notifies.at(i - 1);

				/* Just skip expired entries, deleting on startup is unliked */
				if (ne->expires && ne->expires <= Anope::CurTime)
					continue;
				else if (notifyService.Check(u, ne->mask))
				{
					notifyService.AddMatch(u, ne);
					matched = true;
				}
			}

			if (matched)
				matches++;
		}

		if (matches > 0)
			Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: Matched " << matches << " user(s) against the Notify list";
	}

	void OnModuleLoad(User *, Module *) anope_override
	{
		OnUplinkSync(NULL);
	}

	void OnUserQuit(User *u, const Anope::string &msg) anope_override
	{
		if (notifyService.IsMatch(u))
		{
			if (notifyService.HasFlag(u, 'd'))
				Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << BuildNUHR(u) << " disconnected (reason: " << msg << ")";

			notifyService.DelMatch(u);
		}
	}

	unsigned CheckUser(User *u)
	{
		if (notifyService.GetNotifiesCount() == 0 || (u->server && u->server->IsULined()))
			return 0;

		std::vector<NotifyEntry *> notifies = notifyService.GetNotifies();

		unsigned matches = 0;
		for (unsigned i = notifies.size(); i > 0; --i)
		{
			NotifyEntry *ne = notifies.at(i - 1);

			if (ne->expires && ne->expires <= Anope::CurTime)
				notifyService.Expire(ne);
			else if (notifyService.Check(u, ne->mask))
			{
				if (notifyService.ExistsAlready(u, ne))
					continue;

				notifyService.AddMatch(u, ne);
				matches++;
			}
		}

		return matches;
	}

	void OnUserConnect(User *u, bool &) anope_override
	{
		if (Me && !Me->IsSynced())
			return;

		unsigned matches = CheckUser(u);
		if (matches > 0)
		{
			if (notifyService.HasFlag(u, 'c'))
				Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << BuildNUHR(u) << " connected [matches " << matches << " Notify mask(s)]";
		}
	}

	void OnUserNickChange(User *u, const Anope::string &oldnick) anope_override
	{
		const Anope::string nuhr = oldnick + "!" + u->GetIdent() + "@" + u->host + " (" + u->realname + ")";
		bool oldmatch = false;

		if (notifyService.IsMatch(u))
			oldmatch = true;

		unsigned matches = CheckUser(u);

		if (!notifyService.HasFlag(u, 'n'))
			return;

		if (matches > 0)
		{
			if (oldmatch)
				Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << nuhr << " changed nick to " << u->nick << " [matches an additional " << matches << " Notify mask(s)]";
			else
				Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << nuhr << " changed nick to " << u->nick << " [matches " << matches << " Notify mask(s)]";
		}
		else if (oldmatch)
			Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << nuhr << " changed nick to " << u->nick;
	}

	void OnJoinChannel(User *u, Channel *c) anope_override
	{
		if (notifyService.HasFlag(u, 'j'))
			Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << " joined " << c->name;
	}

	void OnPartChannel(User *u, Channel *c, const Anope::string &channel, const Anope::string &msg) anope_override
	{
		if (notifyService.HasFlag(u, 'p'))
			Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << " parted " << c->name << " (reason: " << msg << ")";
	}

	void OnUserKicked(const MessageSource &source, User *target, const Anope::string &channel, ChannelStatus &status, const Anope::string &kickmsg) anope_override
	{
		User *u = source.GetUser();

		if (notifyService.HasFlag(target, 'k'))
			Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(target) << " was kicked from " << channel << " by " << (u ? u->nick : "unknown") << " (reason: " << kickmsg << ")";

		if (u && notifyService.HasFlag(u, 'k'))
			Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << " kicked " << BuildNUHR(target) << " from "<< channel << " (reason: " << kickmsg << ")";
	}

	void OnUserMode(const MessageSource &setter, User *u, const Anope::string &mname, bool setting)
	{
		const Anope::string &nuhr = BuildNUHR(u);
		UserMode *um = ModeManager::FindUserModeByName(mname);

		if (setter.GetUser() && setter.GetUser() != u)
			Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << setter.GetUser()->nick << (setting ? " set" : " unset") << " mode " << (um ? um->mchar : '\0') << " (" << mname << ")"  << " on " << nuhr;
		else
			Log(LOG_NORMAL, "notify/user", OperServ) << "NOTIFY: " << nuhr << (setting ? " set" : " unset") << " mode " << (um ? um->mchar : '\0') << " (" << mname << ")";
	}

	void OnUserModeSet(const MessageSource &setter, User *u, const Anope::string &mname) anope_override
	{
		if (notifyService.HasFlag(u, 'u'))
			OnUserMode(setter, u, mname, true);
	}

	void OnUserModeUnset(const MessageSource &setter, User *u, const Anope::string &mname) anope_override
	{
		if (notifyService.HasFlag(u, 'u'))
			OnUserMode(setter, u, mname, false);
	}

	void OnChannelMode(Channel *c, MessageSource &setter, ChannelMode *mode, const Anope::string &param, bool setting)
	{
		User *u = setter.GetUser();
		if (!u)
			return;

		if (notifyService.HasFlag(u, 'm'))
		{
			if (mode->type == MODE_STATUS)
			{
				User *target = User::Find(param, false);
				Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << (setting ? " set" : " unset") << " channel mode " << mode->mchar << " (" << mode->name << ") on " << (target ? target->nick : "unknown") << " on " << c->name;
			}
			else
				Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << (setting ? " set" : " unset") << " channel mode " << mode->mchar << " (" << mode->name << ")" << (param.empty() ? "" : " ["+param+"]") << " on " << c->name;
		}
		else if (mode->type == MODE_STATUS)
		{
			User *target = User::Find(param, false);
			if (target && notifyService.HasFlag(target, 'm'))
				Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << u->nick << (setting ? " set" : " unset") << " channel mode " << mode->mchar << " (" << mode->name << ") on " << BuildNUHR(target) << " on " << c->name;
		}
	}

	EventReturn OnChannelModeSet(Channel *c, MessageSource &setter, ChannelMode *mode, const Anope::string &param) anope_override
	{
		OnChannelMode(c, setter, mode, param, true);

		return EVENT_CONTINUE;
	}

	EventReturn OnChannelModeUnset(Channel *c, MessageSource &setter, ChannelMode *mode, const Anope::string &param) anope_override
	{
		OnChannelMode(c, setter, mode, param, false);

		return EVENT_CONTINUE;
	}

	void OnTopicUpdated(User *source, Channel *c, const Anope::string &user, const Anope::string &topic) anope_override
	{
		User *u = source ? source : User::Find(user, false);

		if (u && notifyService.HasFlag(u, 't'))
			Log(LOG_NORMAL, "notify/channel", OperServ) << "NOTIFY: " << BuildNUHR(u) << " changed topic on " << c->name << " to: " << topic;
	}

	void OnPostCommand(CommandSource &source, Command *command, const std::vector<Anope::string> &params) anope_override
	{
		User *u = source.GetUser();
		if (!u || (u && !notifyService.HasFlag(u, 's')))
			return;

		const Anope::string &cmd = command->name;
		if (!notifyService.HasFlag(u, 'S') && (Anope::Match(cmd, "*/set/*")))
			return;

		const Anope::string &nuhr = BuildNUHR(u);
		Anope::string strparams;
		if (!params.empty() && cmd != "nickserv/register" && cmd != "nickserv/identify" && cmd != "nickserv/confirm" &&
		    cmd != "nickserv/group" && cmd != "nickserv/recover" && cmd != "nickserv/set/password" &&
		    cmd != "nickserv/cert" && cmd != "memoserv/send" && cmd != "memoserv/rsend" && cmd != "memoserv/staff")
		{
			for (unsigned i = 0; i < params.size(); ++i)
				strparams.append(params[i] + " ");
			strparams.rtrim(" ");
		}

		Anope::string scmd = source.service->nick + " " + cmd.substr(cmd.find('/') + 1).replace_all_ci("/", " ").upper();

		Log(LOG_NORMAL, "notify/commands", OperServ) << "NOTIFY: " << nuhr << " used " << scmd << (!strparams.empty() ? " ["+strparams+"]" : "");
	}
};

MODULE_INIT(OSNotify)
