/*
 * ChanServ Set JoinFlood
 *
 * (C) 2017 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * A less restrictive join flood protection.
 * If a registered user only channel mode is available,
 * it will be used. Otherwise, a temp ban and kick will be used.
 * Either measure will last for a set number of seconds.
 *
 * Syntax: SET JOINFLOOD channel {ON [joins [secs [duration]]] | OFF | SHOW}
 *
 * Configuration to put into your chanserv config:
module { name = "cs_set_joinflood" }
command { service = "ChanServ"; name = "SET JOINFLOOD"; command = "chanserv/set/joinflood"; }
 *
 */

#include "module.h"


/* Store the settings, joins, and bans  */
struct JoinCounter
{
	unsigned int joins;
	time_t secs;
	time_t duration;

	unsigned int counter;
	time_t reset;
	bool engaged;
	std::vector<Anope::string> banmasks;

	JoinCounter(Extensible *) :
		joins(0), secs(0), duration(0), counter(0), reset(0), engaged(false) { }

	void ResetCounter()
	{
		this->reset = Anope::CurTime + this->secs;
		this->counter = 0;
	}

	bool ShouldReset()
	{
		return (this->reset <= Anope::CurTime);
	}

	bool ShouldEngage()
	{
		return (this->counter >= this->joins);
	}
};

/* Timer to disengage protection after the set duration */
class DisengageTimer : public Timer
{
  private:
	Anope::string channel;
	Anope::string mode;
	char symbol;

  public:
	DisengageTimer(Module *me, time_t seconds, Channel *c, const Anope::string &m, const char &s) : Timer(me, seconds), channel(c->name), mode(m), symbol(s) { }

	void Tick(time_t) anope_override
	{
		Channel *c = Channel::Find(this->channel);
		if (!c)
			return;

		if (!mode.empty())
			c->RemoveMode(c->ci->WhoSends(), mode);

		JoinCounter *jc = c->ci->GetExt<JoinCounter>("joincounter");
		if (jc)
		{
			jc->engaged = false;
			jc->ResetCounter();

			for (std::vector<Anope::string>::iterator it = jc->banmasks.begin(); it != jc->banmasks.end(); ++it)
				c->RemoveMode(c->ci->WhoSends(), "BAN", *it);

			jc->banmasks.clear();
		}

		IRCD->SendNotice(c->ci->WhoSends(), (symbol ? Anope::string(symbol) : "") + c->name, "Join flood protection has disengaged.");
	}
};

class CommandCSSetJoinFlood : public Command
{
 public:
	CommandCSSetJoinFlood(Module *creator, const Anope::string &cname = "chanserv/set/joinflood") : Command(creator, cname, 2, 5)
	{
		this->SetDesc("Enables a join flood protection of allowing registered users only");
		this->SetSyntax("\037channel\037 ON [\037joins\037 [\037secs\037 [\037duration\037]]]");
		this->SetSyntax("\037channel\037 OFF");
		this->SetSyntax("\037channel\037 SHOW");
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (Anope::ReadOnly)
		{
			source.Reply(READ_ONLY_MODE);
			return;
		}

		ChannelInfo *ci = ChannelInfo::Find(params[0]);
		if (ci == NULL)
		{
			source.Reply(CHAN_X_NOT_REGISTERED, params[0].c_str());
			return;
		}

		EventReturn MOD_RESULT;
		FOREACH_RESULT(OnSetChannelOption, MOD_RESULT, (source, this, ci, params[1]));
		if (MOD_RESULT == EVENT_STOP)
			return;

		if (MOD_RESULT != EVENT_ALLOW && !source.AccessFor(ci).HasPriv("SET") && source.permission.empty() && !source.HasPriv("chanserv/administration"))
		{
			source.Reply(ACCESS_DENIED);
			return;
		}

		if (params[1].equals_ci("ON"))
		{
			/* Set defaults for these, then change if the user specifies */
			unsigned int joins = 3;
			time_t secs = 10;
			time_t duration = 60;
			if (params.size() >= 3)
			{
				joins = convertTo<unsigned int>(params[2]);
				if (params.size() >= 4)
					secs = convertTo<time_t>(params[3]);
				if (params.size() == 5)
					duration = convertTo<time_t>(params[4]);
			}

			Log(source.AccessFor(ci).HasPriv("SET") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to enable join flood protection";
			ci->Extend<bool>("JOINFLOOD");
			JoinCounter *jc = ci->Require<JoinCounter>("joincounter");
			if (jc)
			{
				jc->joins = joins;
				jc->secs = secs;
				jc->duration = duration;
			}
			source.Reply("Services will now protect against a join flood (%u joins in %lu seconds) in \002%s\002 by only allowing registered users to join for %lu seconds.", joins, secs, ci->name.c_str(), duration);
		}
		else if (params[1].equals_ci("OFF"))
		{
			Log(source.AccessFor(ci).HasPriv("SET") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to disable join flood protection";
			ci->Shrink<bool>("JOINFLOOD");
			ci->Shrink<JoinCounter>("joincounter");
			source.Reply("Services will no longer protect against a join flood in \002%s\002.", ci->name.c_str());
		}
		else if (params[1].equals_ci("SHOW"))
		{
			JoinCounter *jc = ci->GetExt<JoinCounter>("joincounter");
			if (jc && ci->HasExt("JOINFLOOD"))
				source.Reply("Services will protect against a join flood of %u joins in %lu seconds in \002%s\002 by only allowing registered users to join for %lu seconds.", jc->joins, jc->secs, ci->name.c_str(), jc->duration);
			else
				source.Reply("Join flood protection is not enabled for \002%s\002.", ci->name.c_str());
		}
		else
			return this->OnSyntaxError(source, "JOINFLOOD");
	}

	bool OnHelp(CommandSource &source, const Anope::string &) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Enables or disables a type of joinflood protection where\n"
			"the channel becomes restricted to registered users only.\n"
			" \n"
			"The optional parameters to \002ON\002 are:\n"
			" \n"
			"joins: Number of joins to trigger protection\n"
			"secs: Number of seconds the joins must be within\n"
			"duration: Number of seconds to restrict the channel\n");

		return true;
	}
};

class CSSetJoinFlood : public Module
{
	/* For storing and setting the values of 'joins' per 'seconds', for 'duration'. */
	struct JoinFlood : SerializableExtensibleItem<bool>
	{
		JoinFlood(Module *m, const Anope::string &n) : SerializableExtensibleItem<bool>(m, n) { }

		void ExtensibleSerialize(const Extensible *e, const Serializable *s, Serialize::Data &data) const anope_override
		{
			SerializableExtensibleItem<bool>::ExtensibleSerialize(e, s, data);

			if (s->GetSerializableType()->GetName() != "ChannelInfo")
				return;

			const ChannelInfo *ci = anope_dynamic_static_cast<const ChannelInfo *>(s);
			const JoinCounter *jc = ci->GetExt<JoinCounter>("joincounter");
			if (jc)
			{
				data["jf:joins"] << jc->joins;
				data["jf:secs"] << jc->secs;
				data["jf:duration"] << jc->duration;
			}
		}

		void ExtensibleUnserialize(Extensible *e, Serializable *s, Serialize::Data &data) anope_override
		{
			SerializableExtensibleItem<bool>::ExtensibleUnserialize(e, s, data);

			if (s->GetSerializableType()->GetName() != "ChannelInfo")
				return;

			ChannelInfo *ci = anope_dynamic_static_cast<ChannelInfo *>(s);
			JoinCounter *jc = ci->Require<JoinCounter>("joincounter");
			if (jc)
			{
				data["jf:joins"] >> jc->joins;
				data["jf:secs"] >> jc->secs;
				data["jf:duration"] >> jc->duration;
			}
		}
	} joinflood;

	ExtensibleItem<JoinCounter> joincounter;
	CommandCSSetJoinFlood commandcssetjoinflood;

	char symbol;
	ChannelMode *regonlymode = NULL;

	void Init()
	{
		regonlymode = ModeManager::FindChannelModeByName("REGISTEREDONLY");

		ChannelMode *op = ModeManager::FindChannelModeByName("OP");
		ChannelMode *hop = ModeManager::FindChannelModeByName("HALFOP");
		if (hop)
			symbol = anope_dynamic_static_cast<ChannelModeStatus *>(hop)->symbol;
		else
			symbol = op ? anope_dynamic_static_cast<ChannelModeStatus *>(op)->symbol : 0;
	}

 public:
	CSSetJoinFlood(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		joinflood(this, "JOINFLOOD"), joincounter(this, "joincounter"), commandcssetjoinflood(this)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.2");

		if (Me && Me->IsSynced())
			this->Init();
	}

	void OnUplinkSync(Server*) anope_override
	{
		this->Init();
	}

	void OnJoinChannel(User *u, Channel *c) anope_override
	{
		if (Me && !Me->IsSynced())
			return;
		if (!joinflood.HasExt(c->ci))
			return;
		if (u->IsIdentified(true) || u->server->IsULined() || !u->server->IsSynced())
			return;

		JoinCounter *jc = c->ci->GetExt<JoinCounter>("joincounter");
		if (!jc)
			return;

		/* If user is unregistered and joined while we are engaged, no channel mode was available.
		 * We create a ban mask for them, add it to the ban list and kickban them.
		 * NOTE: This can affect users that join (literally at the same time) as we are engaging.
		 */
		if (jc->engaged)
		{
			Anope::string mask = c->ci->GetIdealBan(u);

			jc->banmasks.push_back(mask);

			c->SetMode(c->ci->WhoSends(), "BAN", mask);
			c->Kick(c->ci->WhoSends(), u, "This channel is currently restricted to registered users only.");

			return;
		}

		/* If we are due to reset, do that. Then increment counter by one for this join. */
		if (jc->ShouldReset())
		{
			jc->ResetCounter();
			jc->counter++;

			return;
		}

		/* Increment counter for this join, check if we should engage or not. */
		jc->counter++;
		if (!jc->ShouldEngage())
			return;

		/* Not due to reset and just hit the join counter limit; we engage, set mode (if available),
		 * and set a Timer to disengage things after 'duration'.
		 */
		jc->engaged = true;
		if (regonlymode)
			c->SetMode(c->ci->WhoSends(), regonlymode);
		new DisengageTimer(this, jc->duration, c, (regonlymode ? regonlymode->name : ""), symbol);
		IRCD->SendNotice(c->ci->WhoSends(), (symbol ? Anope::string(symbol) : "") + c->name, "Join flood protection has engaged; lasting %lu seconds.", jc->duration);
	}

	void OnChanInfo(CommandSource &source, ChannelInfo *ci, InfoFormatter &info, bool show_all) anope_override
	{
		if (!show_all)
			return;

		if (joinflood.HasExt(ci))
			info.AddOption("Join flood protection");
	}
};

MODULE_INIT(CSSetJoinFlood)
