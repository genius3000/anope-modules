/*
 * Support for InspIRCd 2.0 Extras m_joinpartspam.
 *
 * (C) 2019 - Matt Schatz (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Provides the required logic for the mode to be mostly
 * functional with ChanServ MODE but not abusable.
 *
 * Configuration to put into your modules config:
module { name = "m_insp_joinpartspam" }
 *
 */

#include "module.h"


static Module *me;

class ChannelModeJoinPartSpam : public ChannelModeParam
{
	bool ParseCycles(sepstream &stream) const
	{
		Anope::string strcycles;
		if (!stream.GetToken(strcycles))
			return false;

		int result = convertTo<int>(strcycles);
		if (result < 2 || result > 20)
			return false;

		return true;
	}

	bool ParseSeconds(sepstream &stream) const
	{
		Anope::string strseconds;
		if (!stream.GetToken(strseconds))
			return false;

		int result = convertTo<int>(strseconds);
		if (result < 1 || result > 43200)
			return false;

		return true;
	}

 public:
	ChannelModeJoinPartSpam(const Anope::string &modename, const char modechar) : ChannelModeParam(modename, modechar, true) { }

	bool IsValid(Anope::string &value) const anope_override
	{
		sepstream stream(value, ':');

		if (!ParseCycles(stream))
			return false;
		// This checks duration first then block time.
		if (!ParseSeconds(stream) || !ParseSeconds(stream))
			return false;
		// Disallow any redirect from here, we can't verify the parameter.
		if (!stream.StreamEnd())
			return false;

		return true;
	}
};

class InspJoinPartSpam : public Module
{
	const Anope::string modename;
	const char modechar;

 public:
	InspJoinPartSpam(const Anope::string &modname, const Anope::string &creator)
		: Module(modname, creator, THIRD)
		, modename("JOINPARTSPAM")
		, modechar('x')
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		if (!ModuleManager::FindModule("inspircd20"))
			throw ModuleException("This module only works with the InspIRCd 2.0 protocol.");

		ChannelMode *cm = ModeManager::FindChannelModeByChar(modechar);
		if (cm)
			throw ModuleException("A channel mode with character '" + Anope::string(modechar) + "' already exists.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.0");
		me = this;
		ModeManager::AddChannelMode(new ChannelModeJoinPartSpam(modename, modechar));
	}

	~InspJoinPartSpam()
	{
		ChannelMode *cm = ModeManager::FindChannelModeByChar(modechar);
		if (cm)
			ModeManager::RemoveChannelMode(cm);
	}
};

MODULE_INIT(InspJoinPartSpam)
