/*
 * ChanServ AKICK Check
 *
 * (C) 2018 - Matt Schatz (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Check channel AKICKs upon services start, when a user
 * logs in or out of an account, groups a nick, or
 * changes their nickname or displayed host.
 *
 * Configuration to put into your chanserv config:
module { name = "cs_akick_check" }
 */

#include "module.h"


class CSAkickCheck : public Module
{
 public:
	CSAkickCheck(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, THIRD)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.0");
	}

	void OnUplinkSync(Server *) anope_override
	{
		for (channel_map::const_iterator chan = ChannelList.begin(), chan_end = ChannelList.end(); chan != chan_end; ++chan)
		{
			Channel *c = chan->second;
			for (Channel::ChanUserList::iterator it = c->users.begin(), it_end = c->users.end(); it != it_end; )
			{
				ChanUserContainer *uc = it->second;
				++it;

				c->CheckKick(uc->user);
			}
		}
	}

	void CheckAkicks(User *u)
	{
		for (User::ChanUserList::iterator it = u->chans.begin(), it_end = u->chans.end(); it != it_end; )
		{
			ChanUserContainer *cc = it->second;
			++it;

			Channel *c = cc->chan;
			if (c)
				c->CheckKick(u);
		}
	}

	// Hacky way to catch IDENT changes.
	void OnLog(Log *l) anope_override
	{
		if (!Me || !Me->IsSynced() || l->type != LOG_USER || l->u == NULL || l->category != "ident")
			return;

		CheckAkicks(l->u);
	}

	void OnNickGroup(User *u, NickAlias *) anope_override
	{
		CheckAkicks(u);
	}

	void OnNickLogout(User *u) anope_override
	{
		CheckAkicks(u);
	}

	void OnSetDisplayedHost(User *u) anope_override
	{
		CheckAkicks(u);
	}

	void OnUserNickChange(User *u, const Anope::string &) anope_override
	{
		CheckAkicks(u);
	}

	void OnUserLogin(User *u) anope_override
	{
		CheckAkicks(u);
	}
};

MODULE_INIT(CSAkickCheck)
