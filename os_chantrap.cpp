/*
 * OperServ ChanTrap
 *
 * (C) 2017-2018 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Create fake channels or match to wildcard masks to catch unwanted users and/or botnets.
 * User count, modes and action taken can vary by channel/mask.
 *
 * Syntax: CHANTRAP ADD mask botcount action duration modes reason
 *		    DEL {mask | entry-num | list}
 *		    LIST | VIEW [mask | entry-num | list]
 *		    CLEAR
 *
 * Configuration to put into your operserv config:
module { name = "os_chantrap"; killreason = "I know what you did last join!"; akillreason = "You found yourself a disappearing act!"; }
command { service = "OperServ"; name = "CHANTRAP"; command = "operserv/chantrap"; permission = "operserv/chantrap"; }
 *
 * Don't forget to add 'operserv/chantrap' to your oper permissions
 */

#include "module.h"


static ServiceReference<XLineManager> akills("XLineManager", "xlinemanager/sgline");

enum ChanTrapAction
{
	CTA_KILL,
	CTA_AKILL,
	CTA_SIZE
};

/* Dataset for each Chan Trap */
struct ChanTrapInfo : Serializable
{
 public:
	Anope::string mask;	/* Channel mask */
	Anope::string modes;	/* Channel modes */
	unsigned bots;		/* Number of bots to idle */
	ChanTrapAction action;	/* Action to take on joining users */
	time_t duration;	/* Duration of ban (if a ban action) */
	Anope::string creator;	/* Nick of creator */
	Anope::string reason;	/* Reason for this trap */
	time_t created;		/* Time of creation */

	ChanTrapInfo() : Serializable("ChanTrap") { }

	~ChanTrapInfo();

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["mask"] << this->mask;
		data["modes"] << this->modes;
		data["bots"] << this->bots;
		data["action"] << this->action;
		data["duration"] << this->duration;
		data["creator"] << this->creator;
		data["reason"] << this->reason;
		data["created"] << this->created;
	}

	static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

/* We create bots separate of the BotServ system, we don't want these being used elsewhere
 * This class holds the information (User) of a created bot along with its functions
 */
class CreatedBotInfo : public User
{
 public:
	CreatedBotInfo(const Anope::string &_nick) : User(_nick, "ct", Me->GetName(), "", "", Me, "CT Service", Anope::CurTime, "", IRCD ? IRCD->UID_Retrieve() : "", NULL)
	{
		if (Me && Me->IsSynced())
		{
			Anope::string botmodes = Config->GetModule("OperServ")->Get<Anope::string>("modes");
			if (botmodes.empty())
				botmodes = IRCD->DefaultPseudoclientModes;
			if (!botmodes.empty())
				this->SetModesInternal(this, botmodes.c_str());

			IRCD->SendClientIntroduction(this);
		}
	}

	void Join(Channel *c)
	{
		if (c->FindUser(this) != NULL)
			return;

		ChannelStatus status(Config->GetModule("BotServ")->Get<Anope::string>("botmodes", "ao"));
		c->JoinUser(this, &status);
		if (IRCD)
			IRCD->SendJoin(this, c, &status);
	}

	void Part(Channel *c)
	{
		if (c->FindUser(this) == NULL)
			return;

		IRCD->SendPart(this, c, "Chan Trap deleted");
		c->DeleteUser(this);
	}

	~CreatedBotInfo()
	{
		if (Me && Me->IsSynced())
			IRCD->SendQuit(this, "");
	}
};

/* This class holds the list of created bots and any needed functions to this module */
class CreatedBots
{
 protected:
	std::map<const CreatedBotInfo *, unsigned> Bots;

 public:
	CreatedBots() { }

	~CreatedBots()
	{
		for (std::map<const CreatedBotInfo *, unsigned>::reverse_iterator it = Bots.rbegin(); it != Bots.rend(); ++it)
			delete it->first;

		Bots.clear();
	}

	const unsigned GetCount()
	{
		return Bots.size();
	}

	const CreatedBotInfo *Create()
	{
		const unsigned nicklen = Config->GetBlock("networkinfo")->Get<unsigned>("nicklen");
		Anope::string nick;

		unsigned tries = 0;
		do
		{
			nick = "CT" + stringify(static_cast<uint16_t>(rand()));
			if (nick.length() > nicklen)
				nick = nick.substr(0, nicklen);
		}
		while (User::Find(nick) && tries++ < 10);

		/* Should never get to this! */
		if (tries == 11)
			return NULL;

		const CreatedBotInfo *cbi = new CreatedBotInfo(nick);
		Bots.insert(std::make_pair(cbi, 0));
		return cbi;
	}

	const std::map<const CreatedBotInfo *, unsigned> &GetBots()
	{
		return Bots;
	}

	void Join(const CreatedBotInfo *cbi, Channel *c)
	{
		std::map<const CreatedBotInfo*, unsigned>::iterator it = Bots.find(cbi);
		if (it != Bots.end())
		{
			const_cast<CreatedBotInfo *>(cbi)->Join(c);
			++it->second;
		}
	}

	void Part(const CreatedBotInfo *cbi, Channel *c)
	{
		std::map<const CreatedBotInfo*, unsigned>::iterator it = Bots.find(cbi);
		if (it == Bots.end())
			return;

		const_cast<CreatedBotInfo *>(cbi)->Part(c);
		if (--it->second == 0)
		{
			delete (*it).first;
			Bots.erase(it);
		}
	}

	void TryPart(const User *u, Channel *c)
	{
		for (std::map<const CreatedBotInfo *, unsigned>::reverse_iterator it = Bots.rbegin(); it != Bots.rend(); ++it)
		{
			const CreatedBotInfo *cbi = it->first;

			if (cbi->nick == u->nick)
			{
				Part(cbi, c);
				return;
			}
		}
	}
}
CreatedBots;

/* List of Chan Traps */
class ChanTrapList
{
 protected:
	Serialize::Checker<std::vector<ChanTrapInfo *> > chantraps;

 public:
	ChanTrapList() : chantraps("ChanTrap") { }

	~ChanTrapList()
	{
		for (unsigned i = chantraps->size(); i > 0; --i)
			delete chantraps->at(i - 1);
	}

	void Add(ChanTrapInfo *ct)
	{
		chantraps->push_back(ct);
	}

	void Del(ChanTrapInfo *ct)
	{
		if (ct->bots > 0)
		{
			Channel *c = Channel::Find(ct->mask);
			if (c)
			{
				/* Check for Bots in the channel and remove them */
				for (Channel::ChanUserList::const_iterator it = c->users.begin(); it != c->users.end(); )
				{
					const User *u = it->first;
					++it;

					if (u->server != Me)
						continue;

					BotInfo *bi = BotInfo::Find(u->nick, true);
					if (bi)
						bi->Part(c, "Chan Trap deleted");
					else
						CreatedBots.TryPart(u, c);
				}
			}
		}

		std::vector<ChanTrapInfo *>::iterator it = std::find(chantraps->begin(), chantraps->end(), ct);
		if (it != chantraps->end())
			chantraps->erase(it);
	}

	void Clear()
	{
		for (unsigned i = chantraps->size(); i > 0; --i)
			delete (*chantraps).at(i - 1);
	}

	const ChanTrapInfo *Find(const Anope::string &mask)
	{
		for (std::vector<ChanTrapInfo *>::const_iterator it = chantraps->begin(); it < chantraps->end(); ++it)
		{
			const ChanTrapInfo *ct = *it;

			if (Anope::Match(mask, ct->mask, false, true))
				return ct;
		}

		return NULL;
	}

	const ChanTrapInfo *FindExact(const Anope::string &mask)
	{
		for (std::vector<ChanTrapInfo *>::const_iterator it = chantraps->begin(); it < chantraps->end(); ++it)
		{
			const ChanTrapInfo *ct = *it;

			if (ct->mask.equals_ci(mask))
				return ct;
		}

		return NULL;
	}

	const ChanTrapInfo *Get(const unsigned number)
	{
		if (number >= chantraps->size())
			return NULL;

		return chantraps->at(number);
	}

	const unsigned GetCount()
	{
		return chantraps->size();
	}

	const std::vector<ChanTrapInfo *> &GetChanTraps()
	{
		return *chantraps;
	}
}
ChanTrapList;

ChanTrapInfo::~ChanTrapInfo()
{
	ChanTrapList.Del(this);
}

Serializable* ChanTrapInfo::Unserialize(Serializable *obj, Serialize::Data &data)
{
	ChanTrapInfo *ct;

	if (obj)
		ct = anope_dynamic_static_cast<ChanTrapInfo *>(obj);
	else
		ct = new ChanTrapInfo;

	data["mask"] >> ct->mask;
	data["modes"] >> ct->modes;
	data["bots"] >> ct->bots;
	data["duration"] >> ct->duration;
	data["creator"] >> ct->creator;
	data["reason"] >> ct->reason;
	data["created"] >> ct->created;
	unsigned int a;
	data["action"] >> a;
	ct->action = static_cast<ChanTrapAction>(a);

	if (a > CTA_SIZE)
		return NULL;

	if (!obj)
		ChanTrapList.Add(ct);

	return ct;
}

class ChanTrapDelCallback : public NumberList
{
	CommandSource &source;
	unsigned deleted;
	Command *cmd;

 public:
	ChanTrapDelCallback(CommandSource &_source, const Anope::string &numlist, Command *c) : NumberList(numlist, true), source(_source), deleted(0), cmd(c) { }

	~ChanTrapDelCallback()
	{
		if (!deleted)
		{
			source.Reply("No matching entries on the Chan Trap list.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		if (deleted == 1)
			source.Reply("Deleted 1 entry from the Chan Trap list.");
		else
			source.Reply("Deleted %d entries from the Chan Trap list.", deleted);
	}

	void HandleNumber(unsigned number) anope_override
	{
		if (!number)
			return;

		const ChanTrapInfo *ct = ChanTrapList.Get(number - 1);
		if (!ct)
			return;

		Log(LOG_ADMIN, source, cmd) << "to remove " << ct->mask << " from the list";
		++deleted;
		delete ct;
	}
};

/* Common functions */
BotInfo *OperServ;
Anope::string kill_reason;
Anope::string akill_reason;

void ApplyToChan(const ChanTrapInfo *ct, Channel *c)
{
	for (Channel::ChanUserList::const_iterator it = c->users.begin(); it != c->users.end(); )
	{
		User *u = it->first;
		++it;

		if (u->HasMode("OPER") || (u->server && (u->server == Me || u->server->IsULined())))
			continue;

		if (ct->action == CTA_KILL)
			u->Kill(OperServ, kill_reason);
		else if (ct->action == CTA_AKILL && akills)
		{
			const Anope::string akillmask = "*@" + u->host;
			if (akills->HasEntry(akillmask))
				continue;

			time_t expires = ct->duration + Anope::CurTime;

			XLine *x = new XLine(akillmask, ct->creator, expires, akill_reason, XLineManager::GenerateUID());
			akills->AddXLine(x);
			akills->OnMatch(u, x);
		}
	}
}

bool CreateChan(const ChanTrapInfo *ct)
{
	if (ct->bots == 0)
		return false;

	ChannelStatus status(Config->GetModule("BotServ")->Get<Anope::string>("botmodes", "ao"));
	bool created = false;

	/* Create or takeover the channel, remove users and change modes as needed. */
	Channel *c = Channel::FindOrCreate(ct->mask, created);
	OperServ->Join(c, &status);
	if (!created)
	{
		for (Channel::ModeList::const_iterator it = c->GetModes().begin(); it != c->GetModes().end(); )
		{
			const Anope::string mode = it->first, modearg = it->second;
			++it;

			c->RemoveMode(OperServ, mode, modearg, false);
		}
	}
	c->SetModes(OperServ, false, ct->modes.c_str());
	if (!created)
		ApplyToChan(ct, c);

	/* Join other bots up to the requested count */
	unsigned joined = 1;
	for (botinfo_map::const_iterator it = BotListByNick->begin(), it_end = BotListByNick->end(); it != it_end; ++it)
	{
		if (joined == ct->bots)
			return created;

		BotInfo *bi = it->second;
		if (!bi || bi->nick.equals_ci("OperServ"))
			continue;

		bi->Join(c, &status);
		++joined;
	}

	/* Join any already created bots */
	if (ct->bots > joined && CreatedBots.GetCount() > 0)
	{
		const std::map<const CreatedBotInfo *, unsigned> &bots = CreatedBots.GetBots();
		for (std::map<const CreatedBotInfo *, unsigned>::const_iterator it = bots.begin(), it_end = bots.end(); it != it_end; ++it)
		{
			if (joined == ct->bots)
				return created;

			const CreatedBotInfo *cbi = it->first;
			if (cbi)
			{
				CreatedBots.Join(cbi, c);
				++joined;
			}
		}
	}

	/* Create more bots to meet the requested count */
	if (ct->bots > joined)
	{
		do
		{
			const CreatedBotInfo *cbi = CreatedBots.Create();
			if (!cbi)
				continue;

			CreatedBots.Join(cbi, c);
			++joined;
		}
		while (joined < ct->bots);
	}

	return created;
}

const unsigned FindMatches(const ChanTrapInfo *ct)
{
	unsigned matches = 0;

	for (channel_map::const_iterator it = ChannelList.begin(); it != ChannelList.end(); )
	{
		Channel *c = it->second;
		++it;

		if (Anope::Match(c->name, ct->mask, false, true))
		{
			matches++;
			ApplyToChan(ct, c);
		}
	}

	return matches;
}

class CommandOSChanTrap : public Command
{
 private:
	void DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		Anope::string mask, saction, sduration, modes, reason;
		unsigned bots;

		/* Expecting: ADD mask bots action duration modes reason
		 *	      ADD #test99 5 kill 0 +nts test chantrap channel
		 */

		if (params.size() < 7)
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		mask = params[1];
		saction = params[3];
		sduration = params[4];
		modes = params[5];
		reason = params[6];

		try
		{
			bots = convertTo<unsigned>(params[2]);
		}
		catch (const ConvertException &)
		{
			source.Reply("Invalid number of bots: '%s' is not valid for number of bots.", params[2].c_str());
			return;
		}

		if (bots == 0 && mask.replace_all_cs("?*", "").empty())

		{
			source.Reply("The mask must contain at least one non wildcard character.");
			return;
		}
		else if (bots > 0 && mask.find_first_of("?*") != Anope::string::npos)
		{
			source.Reply("An active channel cannot contain wildcard characters.");
			return;
		}

		else if (mask[0] == '/' && mask[mask.length() - 1] == '/')
		{
			if (bots > 0)
			{
				source.Reply("An active channel cannot be a regex mask.");
				return;
			}

			const Anope::string &regexengine = Config->GetBlock("options")->Get<const Anope::string>("regexengine");

			if (regexengine.empty())
			{
				source.Reply("Regex is disabled.");
				return;
			}

			ServiceReference<RegexProvider> provider("Regex", regexengine);
			if(!provider)
			{
				source.Reply("Unable to find regex engine &s.", regexengine.c_str());
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

		const unsigned maxbots = Config->GetModule("os_chantrap")->Get<unsigned>("maxbots", "5");
		const unsigned botcount = BotListByNick->size();
		const bool createbots = Config->GetModule("os_chantrap")->Get<bool>("createbots", "no");
		if (bots > maxbots)
		{
			source.Reply("%d bots is greater than the maximum of %d", bots, maxbots);
			return;
		}
		if (bots > botcount && !createbots)
		{
			source.Reply("%d bots is greater than the current Bot count (%d) and new bot creation is disabled for Chan Traps.", bots, botcount);
			return;
		}

		ChanTrapAction action;
		if (saction.equals_ci("KILL"))
			action = CTA_KILL;
		else if (saction.equals_ci("AKILL"))
			action = CTA_AKILL;
		else
		{
			source.Reply("The given action of %s is invalid.", saction.c_str());
			this->OnSyntaxError(source, "ADD");
			return;
		}

		time_t duration = Anope::DoTime(sduration);
		/* Be the same as AKILL, default to days if not specified */
		if (isdigit(sduration[sduration.length() - 1]))
			duration *= 86400;
		if (action == CTA_KILL)
			duration = 0;

		/* Validate the modes string */
		spacesepstream sep(modes);
		Anope::string sepmodes;
		sep.GetToken(sepmodes);
		unsigned adding = 1;

		for (size_t i = 0; i < sepmodes.length(); ++i)
		{
			switch (sepmodes[i])
			{
				case '+':
				{
					adding = 1;
					break;
				}
				case '-':
				{
					adding = 0;
					break;
				}
				default:
				{
					ChannelMode *cm = ModeManager::FindChannelModeByChar(sepmodes[i]);
					if (!cm)
					{
						source.Reply("Unknown mode character %c.", sepmodes[i]);
						return;
					}

					if (adding && cm->type != MODE_REGULAR)
					{
						source.Reply("Positive modes must not take a parameter.");
						return;
					}

					if (!adding && (cm->type != MODE_REGULAR && cm->type != MODE_PARAM))
					{
						source.Reply("List and status modes are not allowed.");
						return;
					}
				}
			}
		}

		/* Create or modify a Chan Trap Entry */
		ChanTrapInfo *ct = const_cast<ChanTrapInfo *>(ChanTrapList.FindExact(mask));
		bool created = true;
		if (ct)
		{
			created = false;
			delete ct;
		}
		ct = new ChanTrapInfo();

		ct->mask = mask;
		ct->bots = bots;
		ct->action = action;
		ct->duration = duration;
		ct->modes = modes;
		ct->reason = reason;
		ct->creator = source.GetNick();
		ct->created = Anope::CurTime;
		ChanTrapList.Add(ct);

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		Log(LOG_ADMIN, source, this) << "to " << (created ? "add" : "modify") << " a Chan Trap on " << mask << " for reason: " << reason;
		source.Reply("%s a Chan Trap on %s with %d bots and modes %s, action of %s", (created ? "Added" : "Modified"), mask.c_str(), bots, modes.c_str(), saction.c_str());

		/* Non-active channel mask (can be multiple channels):
		 * First find any matching active channels, FindMatches() will also Apply the action
		 * Then find and drop any matching registered channels
		 */
		if (ct->bots == 0)
		{
			unsigned matched = FindMatches(ct);
			unsigned dropped = 0;
			for (registered_channel_map::const_iterator it = RegisteredChannelList->begin(); it != RegisteredChannelList->end(); )
			{
				ChannelInfo *ci = it->second;
				++it;

				if (!Anope::Match(ci->name, ct->mask, false, true))
					continue;

				dropped++;
				delete ci;
			}

			source.Reply("\002%d\002 channel(s) cleared and \002%d\002 channel(s) dropped.", matched, dropped);
		}
		/* Active channel mask (single channel):
		 * If a matching channel is found, CreateChan() will take care of it
		 * Then check if it is registered and drop it (like it's hot!)
		 */
		else
		{
			bool matched = false;
			bool dropped = false;

			Channel *c = Channel::Find(ct->mask);
			if (c)
				matched = true;

			CreateChan(ct);

			ChannelInfo *ci = ChannelInfo::Find(ct->mask);
			if (ci)
			{
				dropped = true;
				delete ci;
			}

			source.Reply("Matched to %s and dropped %s.", (matched ? "a channel" : "no channels"), (dropped ? "a channel" : "no channels"));
		}
	}

	void DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &match = params.size() > 1 ? params[1] : "";

		if (match.empty())
		{
			this->OnSyntaxError(source, "DEL");
			return;
		}

		if (ChanTrapList.GetCount() == 0)
		{
			source.Reply("The chan trap list is empty.");
			return;
		}

		if (isdigit(match[0]) && match.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			ChanTrapDelCallback list(source, match, this);
			list.Process();
		}
		else
		{
			const ChanTrapInfo *ct = ChanTrapList.FindExact(match);

			if (!ct)
			{
				source.Reply("\002%s\002 not found on the Chan Trap list.", match.c_str());
				return;
			}

			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			Log(LOG_ADMIN, source, this) << "to remove " << ct->mask << " from the list";
			source.Reply("\002%s\002 deleted from the Chan Trap list.", ct->mask.c_str());
			delete ct;
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

					const ChanTrapInfo *ct = ChanTrapList.Get(number - 1);
					if (!ct)
						return;

					Anope::string saction;
					if (ct->action == CTA_KILL)
						saction = "KILL";
					else if (ct->action == CTA_AKILL)
						saction = "AKILL";
					else
						return;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
                                        entry["Mask"] = ct->mask;
                                        entry["Creator"] = ct->creator;
                                        entry["Created"] = Anope::strftime(ct->created, source.nc, true);
					entry["Bot Count"] = stringify(ct->bots);
                                        entry["Modes"] = ct->modes;
                                        entry["Action"] = saction;
                                        entry["Ban Duration"] = Anope::Duration(ct->duration, source.nc);
                                        entry["Reason"] = ct->reason;
					list.AddEntry(entry);
				}
			}
			nl_list(source, list, match);
			nl_list.Process();
		}
		else
		{
			for (unsigned i = 0; i < ChanTrapList.GetCount(); ++i)
			{
				const ChanTrapInfo *ct = ChanTrapList.Get(i);
				if (!ct)
					continue;

				if (match.empty() || match.equals_ci(ct->mask) || Anope::Match(ct->mask, match, false, true))
				{
					Anope::string saction;
					if (ct->action == CTA_KILL)
						saction = "KILL";
					else if (ct->action == CTA_AKILL)
						saction = "AKILL";
					else
						continue;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["Mask"] = ct->mask;
					entry["Creator"] = ct->creator;
					entry["Created"] = Anope::strftime(ct->created, source.nc, true);
					entry["Bot Count"] = stringify(ct->bots);
					entry["Modes"] = ct->modes;
					entry["Action"] = saction;
					entry["Ban Duration"] = Anope::Duration(ct->duration, source.nc);
					entry["Reason"] = ct->reason;
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply("No matching entries on the chan trap list.");
		else
		{
			source.Reply("Current chan trap list:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of chan trap list.");
		}
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (ChanTrapList.GetCount() == 0)
		{
			source.Reply("The chan trap list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Mask").AddColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (ChanTrapList.GetCount() == 0)
		{
			source.Reply("The chan trap list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Mask").AddColumn("Creator").AddColumn("Created").AddColumn("Bot Count");
		list.AddColumn("Modes").AddColumn("Action").AddColumn("Ban Duration").AddColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoClear(CommandSource &source)
	{
		if (ChanTrapList.GetCount() == 0)
		{
			source.Reply("The chan trap list is empty.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		ChanTrapList.Clear();

		Log(LOG_ADMIN, source, this) << "to clear the list.";
		source.Reply("The chan trap list has ben cleared.");
	}

	void DoBotCount(CommandSource &source)
	{
		const unsigned count = CreatedBots.GetCount();

		if (count == 0)
			source.Reply("No bots are currently created by chan traps.");
		else if (count == 1)
			source.Reply("Currently there is 1 bot created by chan traps.");
		else
			source.Reply("Currently there are %d bots created by chan traps.", count);

	}

 public:
	CommandOSChanTrap(Module *creator) : Command(creator, "operserv/chantrap", 1, 7)
	{
		this->SetDesc("Set up channel traps for botnets, etc.");
		this->SetSyntax("ADD \037mask\037 \037bot-count\037 \037action\037 \037duration\037 \037modes\037 \037reason\037");
		this->SetSyntax("DEL {\037mask\037 | \037entry-num\037 | \037list\037}");
		this->SetSyntax("LIST [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("VIEW [\037mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("CLEAR");
		this->SetSyntax("BOTCOUNT");
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
			this->DoClear(source);
		else if (subcmd.equals_ci("BOTCOUNT"))
			this->DoBotCount(source);
		else
			this->OnSyntaxError(source, "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Chan Trap allows you to create channel traps for unwanted users or botnets.\n"
			     "A chosen action will be taken on every user joining a trap channel.\n"
			     "Existing channels will be taken over and/or dropped.");
		source.Reply(" ");
		source.Reply("Channels with greater than 0 bots are considered active and must be a real channel name.\n"
			     "Non-active channels can be wildcard matches.");
		source.Reply(" ");
		source.Reply("The \002ADD\002 command requires all 6 parameters.\n"
			     "Mask is a real channel name for active channels or a (wildcard) mask for non-active channels.\n"
			     "Bot Count is how many bots idle in the channel.\n"
			     "Action is one of KILL or AKILL.\n"
			     "Duration is akill duration, ignored for KILL.\n"
			     "Modes will be set and held on an active channel (ex: +nts-k).\n"
			     "Reason is a reason for the Chan Trap.");

		const Anope::string &regexengine = Config->GetBlock("options")->Get<const Anope::string>("regexengine");
		if (!regexengine.empty())
		{
			source.Reply(" ");
			source.Reply("Regex matches are also supported for Non-active channels using the %s engine.\n"
				     "Enclose your pattern in // if this is desired.", regexengine.c_str());
		}

		source.Reply(" ");
		source.Reply("The \002DEL\002, \002LIST\002, and \002VIEW\002 commands can be used with no parameters, with\n"
			     "a mask to match, an entry number, or a list of entry numbers (1-5 or 1,3 format).");
		source.Reply(" ");
		source.Reply("The \002CLEAR\002 command clears all entries of the Chan Trap list.");
		source.Reply(" ");
		source.Reply("The \002BOTCOUNT\002 command shows how many bots have been created by chan traps.");

		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		if (subcommand.equals_ci("ADD"))
			source.Reply("ADD \037mask\037 \037bot-count\037 \037action\037 \037duration\037 \037modes\037 \037reason\037");
		else if (subcommand.equals_ci("DEL"))
			source.Reply("DEL {\037mask\037 | \037entry-num\037 | \037list\037}");
		else
			this->SendSyntax(source);
	}
};

class OSChanTrap : public Module
{
	Serialize::Type chantrapinfo_type;
	CommandOSChanTrap commandoschantrap;

	void Init()
	{
		OperServ = Config->GetClient("OperServ");

		if (ChanTrapList.GetCount() == 0)
			return;

		unsigned matched_chans = 0;
		unsigned created_chans = 0;

		const std::vector<ChanTrapInfo *> &chantraps = ChanTrapList.GetChanTraps();
		for (std::vector<ChanTrapInfo *>::const_iterator it = chantraps.begin(); it != chantraps.end(); ++it)
		{
			const ChanTrapInfo *ct = *it;

			if (ct->bots == 0)
				matched_chans += FindMatches(ct);
			else
			{
				if (CreateChan(ct))
					created_chans++;
				else
					matched_chans++;
			}
		}

		if (matched_chans > 0)
			Log(LOG_ADMIN, "ChanTrap Init", OperServ) << chantraps.size() << " chan trap(s) matched " << matched_chans << " channel(s).";
		if (created_chans > 0)
			Log(LOG_ADMIN, "ChanTrap Init", OperServ) << chantraps.size() << " chan trap(s) created " << created_chans << " channel(s).";
	}

 public:
	OSChanTrap(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		chantrapinfo_type("ChanTrap", ChanTrapInfo::Unserialize), commandoschantrap(this)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.2");

		if (Me && Me->IsSynced())
			this->Init();
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		OperServ = conf->GetClient("OperServ");
		kill_reason = conf->GetModule(this)->Get<Anope::string>("killreason", "I know what you did last join!");
		akill_reason = conf->GetModule(this)->Get<Anope::string>("akillreason", "You found yourself a disappearing act!");
	}

	void OnUplinkSync(Server *) anope_override
	{
		this->Init();
	}

	void OnJoinChannel(User *u, Channel *c) anope_override
	{
		if (u->server && (u->server == Me || u->server->IsULined()))
			return;

		const ChanTrapInfo *ct = ChanTrapList.Find(c->name);
		if (!ct || (ct->action < 0 || ct->action >= CTA_SIZE))
			return;

		if (u->HasMode("OPER"))
		{
			if (ct->bots == 0 && c->users.size() == 1)
			{
				c->SetModes(OperServ, false, ct->modes.c_str());
			}

			return;
		}

		if (ct->action == CTA_KILL)
			u->Kill(OperServ, kill_reason);
		else if (ct->action == CTA_AKILL && akills)
		{
			const Anope::string akillmask = "*@" + u->host;
			if (akills->HasEntry(akillmask))
				return;

			time_t expires = ct->duration + Anope::CurTime;
			XLine *x = new XLine(akillmask, ct->creator, expires, akill_reason, XLineManager::GenerateUID());
			akills->AddXLine(x);
			akills->OnMatch(u, x);
		}
	}

	EventReturn OnPreCommand(CommandSource &source, Command *command, std::vector<Anope::string> &params) anope_override
	{
		if (command->name == "chanserv/info" && params.size() > 0 && source.IsOper())
		{
			const ChanTrapInfo *ct = ChanTrapList.Find(params[0]);
			if (ct)
			{
				source.Reply("Channel \002%s\002 is a trap channel by %s: %s", params[0].c_str(), ct->creator.c_str(), ct->reason.c_str());
				return EVENT_STOP;
			}
		}

		return EVENT_CONTINUE;
	}
};

MODULE_INIT(OSChanTrap)
