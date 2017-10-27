/*
 * OperServ SWhois
 *
 * (C) 2017 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 * Based off (but written from scratch for 2.0.x and) the previous OS_SWHOIS by
 * azander: https://modules.anope.org/index.php?page=view&id=273
 *
 * Assign SWhois messages to users. You can configure it to only allow one SWhois per NickCore (Group)
 * or to allow a separate SWhois per Nick Alias (the 'useaccount' parameter).
 *
 * Syntax: SWHOIS ADD nick swhois
 *		  DEL {nick | entry-num | list}
 *		  LIST | VIEW  [nick | entry-num | list]
 *		  CLEAR [nick]
 *
 * Configuration to put into your operserv config:
module { name = "os_swhois"; useaccount = "no"; notifyonadd = "yes"; notifyonlogin = "yes"; }
command { service = "OperServ"; name = "SWHOIS"; command = "operserv/swhois"; permission = "operserv/swhois"; }
 *
 * Don't forget to add 'operserv/swhois' to your oper permissions
 */

#include "module.h"


namespace
{
	bool useaccount;
	BotInfo *OperServ;
}

/* Individual SWhois entry */
struct SWhoisEntry : Serializable
{
 public:
	Anope::string core;
	Anope::string nick;
	Anope::string swhois;
	Anope::string creator;
	time_t created;

	SWhoisEntry() : Serializable("SWhois") { }

	SWhoisEntry(const Anope::string &c, const Anope::string &n, const Anope::string &s, const Anope::string &cr, time_t cd)
		: Serializable("SWhois"), core(c), nick(n), swhois(s), creator(cr), created(cd) { }

	~SWhoisEntry();

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["core"] << this->core;
		data["nick"] << this->nick;
		data["swhois"] << this->swhois;
		data["creator"] << this->creator;
		data["created"] << this->created;
	}

	static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

/* List of SWhois entries */
class SWhoisList
{
 protected:
	Serialize::Checker<std::vector<SWhoisEntry *> > entries;

 public:
	SWhoisList() : entries("SWhois") { }

	~SWhoisList()
	{
		for (unsigned i = entries->size(); i > 0; --i)
			delete entries->at(i - 1);
	}

	void Add(SWhoisEntry *entry)
	{
		/* Group Nick Aliases together in the list for better listing */
		std::vector<SWhoisEntry *>::iterator it;
		for (it = entries->begin(); it != entries->end(); ++it)
		{
			if ((*it)->core == entry->core)
				break;
		}

		if (it != entries->end())
			entries->insert(it, entry);
		else
			entries->insert(entries->begin(), entry);
	}

	void Del(const SWhoisEntry *entry)
	{
		std::vector<SWhoisEntry *>::iterator it = std::find(entries->begin(), entries->end(), entry);
		if (it == entries->end())
			return;

		const User *u = User::Find(entry->nick);
		if (u)
			IRCD->SendSWhois(OperServ, u->nick, "");
		entries->erase(it);
	}

	bool Del(const Anope::string &nick)
	{
		for (std::vector<SWhoisEntry *>::const_iterator it = entries->begin(), it_end = entries->end(); it != it_end; ++it)
		{
			if ((*it)->nick == nick)
			{
				delete (*it);
				return true;
			}
		}

		return false;
	}

	bool Del(const NickCore *nc)
	{
		bool existed = false;

		for (unsigned i = entries->size(); i > 0; --i)
		{
			const SWhoisEntry *entry = entries->at(i - 1);

			if (entry->core == nc->display)
			{
				delete entry;
				existed = true;
			}
		}

		return existed;
	}

	/* Delete any non-display Nicks from a NickCore's list */
	void DelAliases()
	{
		for (unsigned i = entries->size(); i > 0; --i)
		{
			const SWhoisEntry *entry = entries->at(i - 1);

			if (entry->core != entry->nick)
				delete entry;
		}
	}

	void Clear()
	{
		for (unsigned i = entries->size(); i > 0; --i)
			delete (*entries).at(i - 1);

	}

	unsigned GetCount()
	{
		return entries->size();
	}

	void Update(SWhoisEntry *entry)
	{
		entry->QueueUpdate();
	}

	SWhoisEntry *GetEntry(const unsigned number)
	{
		if (number >= entries->size())
			return NULL;

		return (*entries).at(number);
	}

	SWhoisEntry *GetEntry(const Anope::string &nick)
	{
		for (std::vector<SWhoisEntry *>::const_iterator it = entries->begin(), it_end = entries->end(); it != it_end; ++it)
		{
			if (nick.equals_ci((*it)->nick))
				return *it;
		}

		return NULL;
	}

	const std::vector<SWhoisEntry *> GetEntriesByCore(const NickCore *nc)
	{
		std::vector<SWhoisEntry *> core_entries;

		for (std::vector<SWhoisEntry *>::const_iterator it = entries->begin(), it_end = entries->end(); it != it_end; ++it)
		{
			if ((*it)->core == nc->display)
				core_entries.push_back(*it);
		}

		return core_entries;
	}

	const std::vector<SWhoisEntry *> &GetEntries()
	{
		return *entries;
	}
}
SWhoisList;

SWhoisEntry::~SWhoisEntry()
{
	SWhoisList.Del(this);
}

Serializable* SWhoisEntry::Unserialize(Serializable *obj, Serialize::Data &data)
{
	SWhoisEntry *entry;

	if (obj)
		entry = anope_dynamic_static_cast<SWhoisEntry *>(obj);
	else
		entry = new SWhoisEntry();

	data["core"] >> entry->core;
	data["nick"] >> entry->nick;
	data["swhois"] >> entry->swhois;
	data["creator"] >> entry->creator;
	data["created"] >> entry->created;

	if (!obj)
		SWhoisList.Add(entry);

	return entry;
}

class SWhoisDelCallback : public NumberList
{
	CommandSource &source;
	unsigned deleted;
	Command *cmd;

 public:
	SWhoisDelCallback(CommandSource &_source, const Anope::string &numlist, Command *c) : NumberList(numlist, true), source(_source), deleted(0), cmd(c) { }

	~SWhoisDelCallback()
	{
		if (!deleted)
		{
			source.Reply("No matching entries on the SWhois list.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		if (deleted == 1)
			source.Reply("Deleted 1 entry from the SWhois list.");
		else
			source.Reply("Deleted %d entries from the SWhois list.", deleted);
	}

	void HandleNumber(unsigned number) anope_override
	{
		if (!number || number > SWhoisList.GetCount())
			return;

		const SWhoisEntry *entry = SWhoisList.GetEntry(number - 1);
		if (!entry)
			return;

		Log(LOG_ADMIN, source, cmd) << "to remove " << entry->nick << " from the list";
		delete entry;
		++deleted;
	}
};

class CommandOSSWhois : public Command
{
 private:
	void DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (params.size() < 3)
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		const NickAlias* na = NickAlias::Find(params[1]);
		if (!na || !na->nc)
		{
			source.Reply("Nick %s is not registered.", params[1].c_str());
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		const NickCore *nc = na->nc;
		Anope::string nick = na->nick;
		if (useaccount && nick != nc->display)
			nick = nc->display;

		const Anope::string &swhois = params[2];
		bool created = true;
		if (SWhoisList.Del(nick))
			created = false;

		SWhoisEntry *entry = new SWhoisEntry(nc->display, nick, swhois, source.GetNick(), Anope::CurTime);
		SWhoisList.Add(entry);

		User *u = User::Find(nick);
		if (!u && nick != params[1])
			u = User::Find(params[1]);

		if (u && u->IsIdentified(true))
		{
			IRCD->SendSWhois(OperServ, u->nick, swhois);
			if (Config->GetModule(this->module)->Get<bool>("notifyonadd", "yes"))
				u->SendMessage(OperServ, "A SWhois has been set on you: %s", swhois.c_str());
		}

		Log(LOG_ADMIN, source, this) << "to " << (created ? "add" : "modify") << " a SWhois message on " << nick;
		source.Reply("%s a SWhois message on %s", (created ? "Added" : "Modified"), nick.c_str());
	}

	void DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &match = params.size() > 1 ? params[1] : "";

		if (match.empty())
		{
			this->OnSyntaxError(source, "DEL");
			return;
		}

		if (SWhoisList.GetCount() == 0)
		{
			source.Reply("The SWhois list is empty.");
			return;
		}

		if (isdigit(match[0]) && match.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			SWhoisDelCallback list(source, match, this);
			list.Process();
		}
		else
		{
			const NickAlias *na = NickAlias::Find(match);
			if (!na || !na->nc)
			{
				source.Reply("%s is not a valid Nick Alias.", match.c_str());
				return;
			}

			if (!SWhoisList.Del(match))
			{
				source.Reply("The Nick Alias %s was not found on the SWhois list.", match.c_str());
				return;
			}

			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			Log(LOG_ADMIN, source, this) << "to delete " << match << " from the SWhois list";
			source.Reply("\002%s\002 deleted from the SWhois list.", match.c_str());
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
					if (!number || number > SWhoisList.GetCount())
						return;

					const SWhoisEntry *s_entry = SWhoisList.GetEntry(number - 1);
					if (!s_entry)
						return;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
					entry["Group"] = s_entry->core;
					entry["Nick"] = s_entry->nick;
					entry["SWhois"] = s_entry->swhois;
					entry["Creator"] = s_entry->creator;
					entry["Created"] = Anope::strftime(s_entry->created, source.nc, true);
					list.AddEntry(entry);
				}
			}
			nl_list(source, list, match);
			nl_list.Process();
		}
		else
		{
			const std::vector<SWhoisEntry *> &entries = SWhoisList.GetEntries();
			for (unsigned i = 0; i < entries.size(); ++i)
			{
				const SWhoisEntry *s_entry = entries.at(i);
				if (!s_entry)
					continue;

				if (match.empty() || match.equals_ci(s_entry->nick) || Anope::Match(s_entry->nick, match) || match.equals_ci(s_entry->core) || Anope::Match(s_entry->core, match))
				{
					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["Group"] = s_entry->core;
					entry["Nick"] = s_entry->nick;
					entry["SWhois"] = s_entry->swhois;
					entry["Creator"] = s_entry->creator;
					entry["Created"] = Anope::strftime(s_entry->created, source.nc, true);
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply("No matching entries on the SWhois list.");
		else
		{
			source.Reply("Current SWhois list:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of SWhois list.");
		}
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (SWhoisList.GetCount() == 0)
		{
			source.Reply("The SWhois list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Group");
		if (!useaccount)
			list.AddColumn("Nick");
		list.AddColumn("SWhois");

		this->ProcessList(source, params, list);
	}

	void DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (SWhoisList.GetCount() == 0)
		{
			source.Reply("The SWhois list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Group");
		if (!useaccount)
			list.AddColumn("Nick");
		list.AddColumn("SWhois").AddColumn("Creator").AddColumn("Created");

		this->ProcessList(source, params, list);
	}

	void DoClear(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (SWhoisList.GetCount() == 0)
		{
			source.Reply("The SWhois list is empty.");
			return;
		}

		if (params.size() > 2)
		{
			this->OnSyntaxError(source, "CLEAR");
			return;
		}
		else if (params.size() == 2)
		{
			const NickAlias *na = NickAlias::Find(params[1]);
			if (!na || !na->nc)
			{
				source.Reply("%s is not a valid Nick Alias.", params[1].c_str());
				return;
			}

			if (!SWhoisList.Del(na->nc))
			{
				source.Reply("The group of %s was not found on the SWhois list.", na->nc->display.c_str());
				return;
			}

			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			Log(LOG_ADMIN, source, this) << "to clear the group of " << na->nc->display << " from the SWhois list";
			source.Reply("The group of %s has been cleared from the SWhois list.", na->nc->display.c_str());
		}
		else
		{
			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			SWhoisList.Clear();
			Log(LOG_ADMIN, source, this) << "to clear the SWhois list";
			source.Reply("The SWhois list has been cleared.");
		}
	}

 public:
	CommandOSSWhois(Module *creator) : Command(creator, "operserv/swhois", 1, 3)
	{
		this->SetDesc("Manipulate the SWhois list");
		this->SetSyntax("ADD \037nick\037 \037swhois\037");
		this->SetSyntax("DEL {\037nick\037 | \037entry-num\037 | \037list\037}");
		this->SetSyntax("LIST [\037nick\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("VIEW [\037nick\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("CLEAR [\037nick\037]");
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
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
		else
			this->OnSyntaxError(source, "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Manipulate the SWhois messages assigned to nicks.");
		if (useaccount)
		{
			source.Reply("This network restricts the SWhois to one per Nick Group (account).");
			source.Reply(" ");
			source.Reply("The \002ADD\002 command will assign the given SWhois to the Group that \037nick\037 belongs to.\n"
				     "The \002DEL\002 command will delete the assigned SWhois from the \037nick\037 Group.\n"
				     "The \002LIST\002 command with no parameters will list all nick Groups with a SWhois\n"
				     "assigned to them (and the SWhois message). \002VIEW\002 is more detailed.");
		}
		else
		{
			source.Reply("This network allows a SWhois to be assigned to each Nick Alias (grouped nicks).");
			source.Reply(" ");
			source.Reply("The \002ADD\002 command will assign the given SWhois to the \037nick\037 Alias.\n"
				     "The \002DEL\002 command will delete the assigned SWhois from the \037nick\037 Alias.\n"
				     "The \002LIST\002 command with no parameters will list all Nicks with a SWhois\n"
				     "assigned to them (and the SWhois message). \002VIEW\002 is more detailed.");
		}
		source.Reply("You can filter this with \037nick\037, \037entry number\037, or a \037list\037 (1-3 or 1,3 format).");
		if (useaccount)
			source.Reply("The \002CLEAR\002 command clears all assigned SWhois messages.");
		else
			source.Reply("The \002CLEAR\002 command can be given a \037nick\037 to clear all assigned\n"
				     "SWhois' from that Nick Group. Otherwise it will clear all SWhois messages.");

		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		if (subcommand.equals_ci("ADD"))
			source.Reply("ADD \037nick\037 \037swhois\037");
		else if (subcommand.equals_ci("DEL"))
			source.Reply("DEL {\037nick\037 | \037entry-num\037 | \037list\037}");
		else if (subcommand.equals_ci("CLEAR"))
			source.Reply("CLEAR [\037nick\037]");
		else
			this->SendSyntax(source);
	}
};

class OSSWhois : public Module
{
	Serialize::Type swhoisentry_type;
	CommandOSSWhois commandosswhois;

	void SetSWhois(User *u, const NickAlias *na, const SWhoisEntry *entry)
	{
		IRCD->SendSWhois(OperServ, u->nick, entry->swhois);
		if (Config->GetModule(this)->Get<bool>("notifyonlogin", "yes"))
			u->SendMessage(OperServ, "Your SWhois has been set: %s", entry->swhois.c_str());
	}

	void UnSetSWhois(const User *u)
	{
		IRCD->SendSWhois(OperServ, u->nick, "");
	}

 public:
	OSSWhois(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		swhoisentry_type("SWhois", SWhoisEntry::Unserialize), commandosswhois(this)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.0");
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		bool old_useaccount = useaccount;

		useaccount = conf->GetModule(this)->Get<bool>("useaccount", "no");
		OperServ = conf->GetClient("OperServ");

		/* Changed to single per account, remove any not set to the Group display */
		if (useaccount && !old_useaccount)
			SWhoisList.DelAliases();
	}

	void OnUserLogin(User *u) anope_override
	{
		if (u->Quitting())
			return;

		SWhoisEntry *entry;
		const NickAlias *na = NickAlias::Find(u->nick);
		if (na && (entry = SWhoisList.GetEntry(na->nick)))
			SetSWhois(u, na, entry);
	}

	void OnNickLogout(User *u) anope_override
	{
		if (u->Quitting())
			return;

		const NickAlias *na = NickAlias::Find(u->nick);
		if (na && (SWhoisList.GetEntry(na->nick)))
			UnSetSWhois(u);
	}

	void OnUserNickChange(User *u, const Anope::string &oldnick) anope_override
	{
		if (u->Quitting() || useaccount)
			return;

		SWhoisEntry *entry;
		const NickAlias *na = NickAlias::Find(u->nick);
		const NickAlias *ona = NickAlias::Find(oldnick);

		if (na && (entry = SWhoisList.GetEntry(na->nick)))
			SetSWhois(u, na, entry);
		else if (ona && (SWhoisList.GetEntry(ona->nick)))
			UnSetSWhois(u);
	}

	void OnDelNick(NickAlias *na) anope_override
	{
		if (na->nc)
			SWhoisList.Del(na->nick);
	}

	void OnDelCore(NickCore *nc) anope_override
	{
		SWhoisList.Del(nc);
	}

	void OnChangeCoreDisplay(NickCore *nc, const Anope::string &newdisplay)
	{
		if (SWhoisList.GetCount() == 0)
			return;

		const std::vector<SWhoisEntry *> &entries = SWhoisList.GetEntriesByCore(nc);
		if (entries.empty())
			return;

		for (std::vector<SWhoisEntry *>::const_iterator it = entries.begin(), it_end = entries.end(); it != it_end; ++it)
		{
			SWhoisEntry *entry = *it;

			if (useaccount && entry->nick == entry->core)
				entry->nick = newdisplay;

			entry->core = newdisplay;
			SWhoisList.Update(entry);
		}
	}

	/* Hacky way to catch an Ungroup and update a SWhoisEntry if needed */
	void OnNickCoreCreate(NickCore *nc) anope_override
	{
		if (SWhoisList.GetCount() == 0)
			return;

		SWhoisEntry *entry = SWhoisList.GetEntry(nc->display);
		if (!entry)
			return;

		entry->core = nc->display;
		SWhoisList.Update(entry);
	}

	void OnNickInfo(CommandSource &source, NickAlias *na, InfoFormatter &info, bool show_hidden) anope_override
	{
		if (!show_hidden)
			return;

		const SWhoisEntry *entry = SWhoisList.GetEntry(na->nick);
		if (entry)
			info["SWhois"] = entry->swhois;
	}
};

MODULE_INIT(OSSWhois)
