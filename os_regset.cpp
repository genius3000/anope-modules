/*
 * OperServ RegSet
 *
 * (C) 2020 - Matt Schatz (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Modify the registration time of a nick or channel.
 *
 * Syntax: REGSET {NICK|CHAN} name time
 *
 * Configuration to put into your operserv config:
module { name = "os_regset" }
command { service = "OperServ"; name = "REGSET"; command = "operserv/regset"; permission = "operserv/regset"; }
 *
 * Don't forget to add 'operserv/regset' to your oper permissions.
 */

#include "module.h"


class CommandOSRegSet : public Command
{
 public:
	CommandOSRegSet(Module *creator) : Command(creator, "operserv/regset", 3, 3)
	{
		this->SetDesc(_("Modify the registration time of a nick or channel"));
		this->SetSyntax(_("{NICK|CHAN} \037name\037 \037time\037"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		const Anope::string &targtype = params[0];
		const Anope::string &target = params[1];
		const Anope::string &timestamp = params[2];
		time_t ts;

		if (Anope::ReadOnly)
		{
			source.Reply(READ_ONLY_MODE);
			return;
		}

		if (timestamp.find_first_not_of("1234567890") != Anope::string::npos)
		{
			source.Reply("Invalid timestamp given: '%s' contains non-numeric characters.", timestamp.c_str());
			return;
		}

		try
		{
			ts = convertTo<time_t>(timestamp);
			if (ts <= 0 || ts >= Anope::CurTime)
			{
				source.Reply("Invalid timestamp given: '%s' is out of allowable range.", timestamp.c_str());
				return;
			}
		}
		catch (const ConvertException &)
		{
			source.Reply("Invalid timestamp given: '%s' threw an error on convert.", timestamp.c_str());
			return;
		}

		if (targtype.equals_ci("NICK"))
		{
			NickAlias *na = NickAlias::Find(target);
			if (!na)
			{
				source.Reply(NICK_X_NOT_REGISTERED, target.c_str());
				return;
			}

			if (na->time_registered == ts)
			{
				source.Reply("Current registration time is the same as the given time.");
				return;
			}

			na->time_registered = ts;
			Log(LOG_ADMIN, source, this) << "to modify the registration time on " << na->nick << " to: " << Anope::strftime(ts, NULL, true) << " (" << ts << ")";
			source.Reply("The registration time of %s has been modified to %s (%lu)", na->nick.c_str(), Anope::strftime(ts, source.GetAccount()).c_str(), ts);
		}
		else if (targtype.equals_ci("CHAN"))
		{
			ChannelInfo *ci = ChannelInfo::Find(target);
			if (!ci)
			{
				source.Reply(CHAN_X_NOT_REGISTERED, target.c_str());
				return;
			}

			if (ci->time_registered == ts)
			{
				source.Reply("Current registration time is the same as the given time.");
				return;
			}

			ci->time_registered = ts;
			Log(LOG_ADMIN, source, this) << "to modify the registration time on " << ci->name << " to: " << Anope::strftime(ts, NULL, true) << " (" << ts << ")";
			source.Reply("The registration time of %s has been modified to %s (%lu)", ci->name.c_str(), Anope::strftime(ts, source.GetAccount()).c_str(), ts);
		}
		else
		{
			source.Reply("Invalid target type given.");
			this->SendSyntax(source);
		}
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Allows an administrator to modify the registration time of a nick or channel.");
		source.Reply(" ");
		source.Reply("\002NICK|CHAN\002 is the literal word and is used to specify which you are acting upon.\n"
			     "\037name\037 is either the nickname or channel name.\n"
			     "\037time\037 is the Unix timestamp to set the registration time to.");
		return true;
	}
};

class OSRegSet : public Module
{
	CommandOSRegSet commandosregset;

 public:
	OSRegSet(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		commandosregset(this)
	{
		if(Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.0");
	}
};

MODULE_INIT(OSRegSet)
