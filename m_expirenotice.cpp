/*
 * Notification of pending expiry or expired nicks and channels
 *
 * (C) 2016 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Runs when NickServ and ChanServ check for expired
 * entries. Is capable of sending Notices via email or memo
 * for soon to expire or expired nicknames and channels.
 *
 * The Nick and Channel Expiry defaults are the same as Anope's defaults
 * in the case that the config values aren't read.
 * Configuration to put into your modules config:
module
{
	name = "m_expirenotice"

	ns_notice_expiring = yes
	ns_notice_expired = yes
	ns_notice_time = 7d
	ns_notice_mail = yes
	ns_notice_memo = no

	cs_notice_expiring = yes
	cs_notice_expired = yes
	cs_notice_time = 3d
	cs_notice_mail = yes
	cs_notice_memo = no

	ns_expiring_subject = "Nickname expiring"
	ns_expiring_message = "Your nickname %n will expire %t.
			       %N IRC Administration"

	ns_expiring_memo = "Your nickname %n will expire %t."

	ns_expired_subject = "Nickname expired"
	ns_expired_message = "Your nickname %n has expired.
			      %N IRC Administration"

	ns_expired_memo = "Your nickname %n has expired."

	cs_expiring_subject = "Channel expiring"
	cs_expiring_message = "Your channel %c will expire %t.
			       %N IRC Administration"

	cs_expiring_memo = "Your channel %c will expire %t."

	cs_expired_subject = "Channel expired"
	cs_expired_message = "Your channel %c has expired.
			      %N IRC Administration"

	cs_expired_memo = "Your channel %c has expired."
}
 *
 * Logging of "soon to expire" nicks or channels can be enabled by using
 * "nickserv/preexpire" and "chanserv/preexpire" in the "other" category
 */

#include "module.h"


static ServiceReference<MemoServService> memoserv("MemoServService", "MemoServ");

class ExpireNotice : public Module
{
	bool ns_notice_expiring, ns_notice_expired, ns_notice_mail, ns_notice_memo;
	bool cs_notice_expiring, cs_notice_expired, cs_notice_mail, cs_notice_memo;
	time_t ns_expire_time, ns_notice_time;
	time_t cs_expire_time, cs_notice_time;
	time_t expiretimeout;
	Anope::string networkname;

	/* We check this to prevent a race condition of sending
	 * a memo to a currently expiring NickCore. It seems
	 * we mess up MemoServ when we do that.
	 */
	bool AllAliasesExpiring(NickCore *nc)
	{
		for (unsigned i = 0; i < nc->aliases->size(); ++i)
		{
			NickAlias *na = nc->aliases->at(i);
			if (Anope::CurTime - na->last_seen < ns_expire_time)
				return false;
		}
		return true;
	}

 public:
	ExpireNotice(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		if (!ModuleManager::FindModule("nickserv") && !ModuleManager::FindModule("chanserv"))
			throw ModuleException("Neither NickServ nor ChanServ are loaded, this module is useless!");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.4");
	}

	void OnPreNickExpire(NickAlias *na, bool &expire) anope_override
	{
		/* If expired, not enabled or neither notice method is enabled, we do nothing */
		if (expire || !ns_notice_expiring || (!ns_notice_mail && !ns_notice_memo))
			return;
		/* We don't do anything with unconfirmed or no_expire nicks */
		if (na->nc->HasExt("UNCONFIRMED") || na->HasExt("NS_NO_EXPIRE"))
			return;

		/* If notice_time is set too high, make it a quarter of the expire time */
		if (ns_notice_time >= ns_expire_time)
			ns_notice_time = ns_expire_time / 4;

		time_t expire_at = na->last_seen + ns_expire_time;
		time_t notice_at = expire_at - ns_notice_time;

		/* Send notice when time is between the notice_at and the next ExpireTick
		 * This should keep from sending multiple notices
		 */
		if (Anope::CurTime >= notice_at && Anope::CurTime <= notice_at + expiretimeout - 2)
		{
			Log(LOG_NORMAL, "nickserv/preexpire", Config->GetClient("NickServ")) << "Soon to expire nickname " << na->nick << " (group: " << na->nc->display << "). Expires: " << Anope::strftime(expire_at);

			if (ns_notice_mail && !na->nc->email.empty())
			{
				Anope::string subject = Config->GetModule(this)->Get<const Anope::string>("ns_expiring_subject"),
					message = Config->GetModule(this)->Get<const Anope::string>("ns_expiring_message");
				message = message.replace_all_cs("%n", na->nick);
				message = message.replace_all_cs("%t", Anope::strftime(expire_at, na->nc));
				message = message.replace_all_cs("%N", networkname);

				Mail::Send(na->nc, subject, message);
			}
			/* If the NickCore has more than one NickAlias (not all expiring right now), send a memo */
			if (ns_notice_memo && na->nc->aliases->size() > 1 && !AllAliasesExpiring(na->nc))
			{
				Anope::string message = Config->GetModule(this)->Get<const Anope::string>("ns_expiring_memo");
				message = message.replace_all_cs("%n", na->nick);
				message = message.replace_all_cs("%t", Anope::strftime(expire_at, na->nc));

				memoserv->Send(Config->GetClient("NickServ")->nick, na->nc->display, message, true);
			}
		}
	}

	void OnNickExpire(NickAlias *na) anope_override
	{
		/* Do nothing if not enabled or neither notice method is enabled */
		if (!ns_notice_expired || (!ns_notice_mail && ! ns_notice_memo))
			return;

		if (ns_notice_mail && !na->nc->email.empty())
		{
			Anope::string subject = Config->GetModule(this)->Get<const Anope::string>("ns_expired_subject"),
				message = Config->GetModule(this)->Get<const Anope::string>("ns_expired_message");
			message = message.replace_all_cs("%n", na->nick);
			message = message.replace_all_cs("%N", networkname);

			Mail::Send(na->nc, subject, message);
		}
		/* If the NickCore has more than one NickAlias (not all expiring right now), send a memo */
		if (ns_notice_memo && na->nc->aliases->size() > 1 && !AllAliasesExpiring(na->nc))
		{
			Anope::string message = Config->GetModule(this)->Get<const Anope::string>("ns_expired_memo");
			message = message.replace_all_cs("%n", na->nick);

			memoserv->Send(Config->GetClient("NickServ")->nick, na->nc->display, message, true);
		}
	}

	void OnPreChanExpire(ChannelInfo *ci, bool &expire) anope_override
	{
		/* Do nothing if expired, not enabled or neither notice method is enabled */
		if (expire || !cs_notice_expiring || (!cs_notice_mail && !cs_notice_memo))
			return;
		/* We don't do anything with no_expire chans */
		if (ci->HasExt("CS_NO_EXPIRE"))
			return;

		/* If notice_time is set too high, make it a quarter of the expire time */
		if (cs_notice_time >= cs_expire_time)
			cs_notice_time = cs_expire_time / 4;

		time_t expire_at = ci->last_used + cs_expire_time;
		time_t notice_at = expire_at - cs_notice_time;

		/* Send notice when time is between the notice_at and the next ExpireTick
		 * This should keep from sending multiple notices
		 */
		if (Anope::CurTime >= notice_at && Anope::CurTime <= notice_at + expiretimeout - 2)
		{
			/* Anope only checks for Access of Users in the channel if said channel
			 * is slated to expire right now. We need to run this check here to skip
			 * sending a false notice. We don't update ci->last_used time though.
			 */
			if (ci->c)
			{
				AccessGroup ag;

				for (Channel::ChanUserList::const_iterator cit = ci->c->users.begin(), cit_end = ci->c->users.end(); cit != cit_end; ++cit)
				{
					ag = ci->AccessFor(cit->second->user, false);
					/* If this user has Channel Access, we stop now */
					if (!ag.empty() || ag.founder)
						return;
				}
			}

			NickCore *founder = ci->GetFounder(),
				*successor = ci->GetSuccessor();

			Log(LOG_NORMAL, "chanserv/preexpire", Config->GetClient("ChanServ")) << "Soon to expire channel " << ci->name << " (founder: " << (founder ? founder->display : "(none)") << ") (successor: " << (successor ? successor->display : "(none)") << "). Expires: " << Anope::strftime(expire_at);

			if (cs_notice_mail)
			{
				Anope::string subject = Config->GetModule(this)->Get<const Anope::string>("cs_expiring_subject"),
					message = Config->GetModule(this)->Get<const Anope::string>("cs_expiring_message");
				message = message.replace_all_cs("%c", ci->name);
				message = message.replace_all_cs("%N", networkname);

				if (founder && !founder->email.empty())
				{
					message = message.replace_all_cs("%t", Anope::strftime(expire_at, founder));

					Mail::Send(founder, subject, message);
				}
				if (successor && !successor->email.empty())
				{
					message = message.replace_all_cs("%t", Anope::strftime(expire_at, successor));

					Mail::Send(successor, subject, message);
				}
			}
			if (cs_notice_memo)
			{
				Anope::string message = Config->GetModule(this)->Get<const Anope::string>("cs_expiring_memo");
				message = message.replace_all_cs("%c", ci->name);

				if (founder && !AllAliasesExpiring(founder))
				{
					message = message.replace_all_cs("%t", Anope::strftime(expire_at, founder));

					memoserv->Send(Config->GetClient("ChanServ")->nick, founder->display, message, true);
				}
				if (successor && !AllAliasesExpiring(successor))
				{
					message = message.replace_all_cs("%t", Anope::strftime(expire_at, successor));

					memoserv->Send(Config->GetClient("ChanServ")->nick, successor->display, message, true);
				}
			}
		}
	}

	void OnChanExpire(ChannelInfo *ci) anope_override
	{
		/* Do nothing if not enabled or neither notice method is enabled */
		if (!cs_notice_expired || (!cs_notice_mail && !cs_notice_memo))
			return;

		NickCore *founder = ci->GetFounder(),
			*successor = ci->GetSuccessor();

		if (cs_notice_mail)
		{
			Anope::string subject = Config->GetModule(this)->Get<const Anope::string>("cs_expired_subject"),
				message = Config->GetModule(this)->Get<const Anope::string>("cs_expired_message");
			message = message.replace_all_cs("%c", ci->name);
			message = message.replace_all_cs("%N", networkname);

			if (founder && !founder->email.empty())
				Mail::Send(founder, subject, message);
			if (successor && !successor->email.empty())
				Mail::Send(successor, subject, message);
		}
		if (cs_notice_memo)
		{
			Anope::string message = Config->GetModule(this)->Get<const Anope::string>("cs_expired_memo");
			message = message.replace_all_cs("%c", ci->name);

			if (founder && !AllAliasesExpiring(founder))
				memoserv->Send(Config->GetClient("ChanServ")->nick, founder->display, message, true);
			if (successor && !AllAliasesExpiring(successor))
				memoserv->Send(Config->GetClient("ChanServ")->nick, successor->display, message, true);
		}
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		/* Load configuration values at Config read */
		ns_notice_expiring = Config->GetModule(this)->Get<bool>("ns_notice_expiring", "no");
		ns_notice_expired = Config->GetModule(this)->Get<bool>("ns_notice_expired", "no");
		ns_notice_mail = Config->GetModule(this)->Get<bool>("ns_notice_mail", "no");
		ns_notice_memo = Config->GetModule(this)->Get<bool>("ns_notice_memo", "no");
		ns_notice_time = Config->GetModule(this)->Get<time_t>("ns_notice_time", "7d");
		ns_expire_time = Config->GetModule("nickserv")->Get<time_t>("expire", "21d");

		cs_notice_expiring = Config->GetModule(this)->Get<bool>("cs_notice_expiring", "no");
		cs_notice_expired = Config->GetModule(this)->Get<bool>("cs_notice_expired", "no");
		cs_notice_mail = Config->GetModule(this)->Get<bool>("cs_notice_mail", "no");
		cs_notice_memo = Config->GetModule(this)->Get<bool>("cs_notice_memo", "no");
		cs_notice_time = Config->GetModule(this)->Get<time_t>("cs_notice_time", "3d");
		cs_expire_time = Config->GetModule("chanserv")->Get<time_t>("expire", "14d");

		expiretimeout = Config->GetBlock("options")->Get<time_t>("expiretimeout", "30m");
		networkname = Config->GetBlock("networkinfo")->Get<const Anope::string>("networkname");

		if (!Config->GetBlock("mail")->Get<bool>("usemail"))
			ns_notice_mail = cs_notice_mail = false;
		if (!memoserv)
			ns_notice_memo = cs_notice_memo = false;
	}
};

MODULE_INIT(ExpireNotice)
