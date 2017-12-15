/*
 * ChanServ Topic History
 *
 * (C) 2017 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Keep a history of topics per channel, allow listing and setting from the history
 *
 * Syntax: SET TOPICHISTORY channel {ON | OFF}
 * Syntax: TOPICHISTORY channel {LIST | CLEAR | SET entry-num}
 *
 * Configuration to put into your chanserv config:
module { name = "cs_topichistory"; maxhistory = 3; }
command { service = "ChanServ"; name = "SET TOPICHISTORY"; command = "chanserv/set/topichistory"; }
command { service = "ChanServ"; name = "TOPICHISTORY"; command = "chanserv/topichistory"; group = "chanserv/management"; }
 *
 */

#include "module.h"


/* Individual Topic History entries */
struct TopicHistoryEntry : Serializable
{
 public:
	Anope::string chan;
	Anope::string topic;
	Anope::string setter;
	time_t when;

	TopicHistoryEntry() : Serializable("TopicHistory") { }

	TopicHistoryEntry(ChannelInfo *c, const Anope::string &ctopic, const Anope::string &csetter, time_t ctime = Anope::CurTime) : Serializable("TopicHistory")
	{
		this->chan = c->name;
		this->topic = ctopic;
		this->setter = csetter;
		this->when = ctime;
	}

	~TopicHistoryEntry();

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["chan"] << this->chan;
		data["topic"] << this->topic;
		data["setter"] << this->setter;
		data["when"] << this->when;
	}

	static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

/* Per channel List of Topic History Entries */
struct TopicHistoryList : Serialize::Checker<std::vector<TopicHistoryEntry *> >
{
 public:
	TopicHistoryList(Extensible *) : Serialize::Checker<std::vector<TopicHistoryEntry *> >("TopicHistory") { }

	~TopicHistoryList()
	{
		for (unsigned i = (*this)->size(); i > 0; --i)
			delete (*this)->at(i - 1);
	}
};

TopicHistoryEntry::~TopicHistoryEntry()
{
	ChannelInfo *ci = ChannelInfo::Find(this->chan);
	if (!ci)
		return;

	TopicHistoryList *entries = ci->GetExt<TopicHistoryList>("topichistorylist");
	if (!entries)
		return;

	std::vector<TopicHistoryEntry *>::iterator it = std::find((*entries)->begin(), (*entries)->end(), this);
	if (it != (*entries)->end())
		(*entries)->erase(it);
}

Serializable* TopicHistoryEntry::Unserialize(Serializable *obj, Serialize::Data &data)
{
	Anope::string schan, stopic, ssetter;
	time_t swhen;

	data["chan"] >> schan;

	ChannelInfo *ci = ChannelInfo::Find(schan);
	if (!ci)
		return NULL;

	if (obj)
	{
		TopicHistoryEntry *entry = anope_dynamic_static_cast<TopicHistoryEntry *>(obj);
		entry->chan = ci->name;
		data["topic"] >> entry->topic;
		data["setter"] >> entry->setter;
		data["when"] >> entry->when;
		return entry;
	}

	data["topic"] >> stopic;
	data["setter"] >> ssetter;
	data["when"] >> swhen;
	TopicHistoryEntry *entry = new TopicHistoryEntry(ci, stopic, ssetter, swhen);

	TopicHistoryList *entries = ci->Require<TopicHistoryList>("topichistorylist");
	(*entries)->insert((*entries)->begin(), entry);
	return entry;
}

/* This is set during load and config reload */
unsigned maxhistory = 0;

class CommandCSTopicHistory : public Command
{
 private:
	void DoList(CommandSource &source, ChannelInfo *ci)
	{
		TopicHistoryList *entries = ci->Require<TopicHistoryList>("topichistorylist");

		/* First entry is the current topic, we hide that */
		if ((*entries)->size() <= 1)
		{
			source.Reply("Topic history list for \002%s\002 is empty.", ci->name.c_str());
			return;
		}

		source.Reply("Topic history list for \002%s\002:", ci->name.c_str());

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Set").AddColumn("By").AddColumn("Topic");
		for (unsigned i = 1; i < (*entries)->size(); ++i)
		{
			TopicHistoryEntry *entry = (*entries)->at(i);

			ListFormatter::ListEntry le;
			le["Number"] = stringify(i);
			le["Set"] = Anope::strftime(entry->when, NULL, true);
			le["By"] = entry->setter;
			le["Topic"] = entry->topic;
			list.AddEntry(le);
		}

		std::vector<Anope::string> replies;
		list.Process(replies);
		for (unsigned i = 0; i < replies.size(); ++i)
			source.Reply(replies[i]);

		source.Reply("End of topic history list.");
	}

	void DoClear(CommandSource &source, ChannelInfo *ci)
	{
		/* Removing the List deletes all entries tied to it */
		ci->Shrink<TopicHistoryList>("topichistorylist");
		/* Create a new List and add the current topic, just like when enabling the option */
		TopicHistoryList *entries = ci->Require<TopicHistoryList>("topichistorylist");
		if ((*entries)->empty())
			(*entries)->push_back(new TopicHistoryEntry(ci, ci->last_topic, ci->last_topic_setter, ci->last_topic_time));

		Log(source.AccessFor(ci).HasPriv("TOPIC") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to remove all historical topics.";
		source.Reply("Topic history for \002%s\002 has been cleared.", ci->name.c_str());
	}

	void DoSet(CommandSource &source, ChannelInfo *ci, const Anope::string &entrynum)
	{
		TopicHistoryList *entries = ci->Require<TopicHistoryList>("topichistorylist");

		if ((*entries)->empty())
		{
			source.Reply("Topic history list for \002%s\002 is empty.", ci->name.c_str());
			return;
		}
		else if (!entrynum.is_pos_number_only())
		{
			source.Reply("Topic history \002%s\002 not found for channel \002%s\002.", entrynum.c_str(), ci->name.c_str());
			return;
		}

		try
		{
			unsigned i = convertTo<unsigned>(entrynum);
			if (i > 0 && i <= (*entries)->size())
			{
				if (ci->c->topic == (*entries)->at(i)->topic)
				{
					source.Reply("History entry number \002%u\002 is already the topic for \002%s\002.", i, ci->name.c_str());
					return;
				}

				bool has_topiclock = ci->HasExt("TOPICLOCK");
				ci->Shrink<bool>("TOPICLOCK");
				ci->c->ChangeTopic(source.GetNick(), (*entries)->at(i)->topic, Anope::CurTime);
				if (has_topiclock)
					ci->Extend<bool>("TOPICLOCK");

				Log(source.AccessFor(ci).HasPriv("TOPIC") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to set a historical topic.";
				source.Reply("Setting topic on \002%s\002 to history entry number \002%u\002", ci->name.c_str(), i);
			}
			else
				throw ConvertException();
		}
		catch (const ConvertException &)
		{
			source.Reply("Topic history \002%s\002 not found for channel \002%s\002", entrynum.c_str(), ci->name.c_str());
		}
	}

 public:
	CommandCSTopicHistory(Module *creator) : Command(creator, "chanserv/topichistory", 2, 3)
	{
		this->SetDesc("Maintain a channel's topic history.");
		this->SetSyntax("\037channel\037 LIST");
		this->SetSyntax("\037channel\037 CLEAR");
		this->SetSyntax("\037channel\037 SET \037entry-num\037");
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		const Anope::string &subcmd = params[1];

		ChannelInfo *ci = ChannelInfo::Find(params[0]);
		if (ci == NULL)
			source.Reply(CHAN_X_NOT_REGISTERED, params[0].c_str());
		else if (!source.AccessFor(ci).HasPriv("TOPIC") && !source.HasCommand("chanserv/topic"))
			source.Reply(ACCESS_DENIED);
		else if (!ci->HasExt("TOPICHISTORY"))
			source.Reply("Topic history not enabled for \002%s\002.", ci->name.c_str());
		else if (subcmd.equals_ci("LIST"))
			this->DoList(source, ci);
		else if (subcmd.equals_ci("CLEAR"))
			this->DoClear(source, ci);
		else if (!ci->c)
			source.Reply(CHAN_X_NOT_IN_USE, ci->name.c_str());
		else if (subcmd.equals_ci("SET") && params.size() == 3)
			this->DoSet(source, ci, params[2]);
		else
			this->OnSyntaxError(source, "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Maintain the Topic History for a channel.");
		source.Reply(" ");
		source.Reply("The \002LIST\002 command displays a listing of\n"
			     "historical topics that can be restored.");
		source.Reply(" ");
		source.Reply("The \002CLEAR\002 command clears the list.");
		source.Reply(" ");
		source.Reply("The \002SET\002 command sets the channel topic\n"
			     "to the specified historical topic.");

		return true;
	}
};

class CommandCSSetTopicHistory : public Command
{
 public:
	CommandCSSetTopicHistory(Module *creator, const Anope::string &cname = "chanserv/set/topichistory") : Command(creator, cname, 2, 2)
	{
		this->SetDesc("Enables topic history (list and set previous topics)");
		this->SetSyntax("\037channel\037 {ON | OFF}");
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
			Log(source.AccessFor(ci).HasPriv("SET") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to enable topichistory";
			source.Reply("Topic history option for %s is now \002on\002.", ci->name.c_str());

			ci->Extend<bool>("TOPICHISTORY");
			/* If this channel's topic history list is empty, add the current topic as a starting point. */
			TopicHistoryList *entries = ci->Require<TopicHistoryList>("topichistorylist");
			if ((*entries)->empty())
				(*entries)->push_back(new TopicHistoryEntry(ci, ci->last_topic, ci->last_topic_setter, ci->last_topic_time));
		}
		else if (params[1].equals_ci("OFF"))
		{
			Log(source.AccessFor(ci).HasPriv("SET") ? LOG_COMMAND : LOG_OVERRIDE, source, this, ci) << "to disable topichistory";
			source.Reply("Topic history option for %s is now \002off\022.", ci->name.c_str());

			ci->Shrink<bool>("TOPICHISTORY");
			ci->Shrink<TopicHistoryList>("topichistorylist");
		}
		else
			this->OnSyntaxError(source, "TOPICHISTORY");
	}

	bool OnHelp(CommandSource &source, const Anope::string &) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Enables or disables a history of channel topics.");
		source.Reply(" ");
		source.Reply("The \002ON\002 command enables the option.");
		source.Reply(" ");
		source.Reply("The \002OFF\002 command clears the list and disables the option.");
		source.Reply(" ");
		source.Reply("There is a maximum Topic History list size of %d topics.", maxhistory);
		source.Reply(" ");

		/* Look up and display the proper Bot nick and Command name for using this option */
		Anope::string cmd;
		BotInfo *bi;
		if (Command::FindCommandFromService("chanserv/topichistory", bi, cmd))
			source.Reply("See the help for %s %s on how to use this option.", bi->nick.c_str(), cmd.c_str());
		else
			source.Reply("The required \037chanserv/topichistory\037 command is not enabled, this option is useless.");

		return true;
	}
};

class CSTopicHistory : public Module
{
	Serialize::Type topichistory_type;
	SerializableExtensibleItem<bool> topichistory;
	ExtensibleItem<TopicHistoryList> topichistorylist;
	CommandCSTopicHistory commandcstopichistory;
	CommandCSSetTopicHistory commandcssettopichistory;

 public:

	CSTopicHistory(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		topichistory_type("TopicHistory", TopicHistoryEntry::Unserialize),
		topichistory(this, "TOPICHISTORY"), topichistorylist(this, "topichistorylist"),
		commandcstopichistory(this), commandcssettopichistory(this)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("1.0.1");
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		/* A default of 3 seems decent and set safe limits; 20 is more than enough I think...
		 * NOTE: We actually allow 1 more than "maxhistory" and hide the first entry (index 0)
		 *       This hides the current topic and shows "maxhistory" historical topics
		 */
		maxhistory = conf->GetModule(this)->Get<unsigned>("maxhistory", "3");

		if (maxhistory < 1)
			maxhistory = 1;
		else if (maxhistory > 20)
			maxhistory = 20;
	}

	void OnTopicUpdated(User *source, Channel *c, const Anope::string &user, const Anope::string &topic) anope_override
	{
		/* Don't update topic history during a Server Sync */
		if (Me && !Me->IsSynced())
			return;
		if (!c->ci)
			return;
		if (!c->ci->HasExt("TOPICHISTORY"))
			return;

		TopicHistoryList *entries = c->ci->Require<TopicHistoryList>("topichistorylist");

		/* If the new topic matches an existing one, delete the current entry */
		if (!(*entries)->empty())
		{
			for (unsigned i = 0; i < (*entries)->size(); ++i)
			{
				if (topic == (*entries)->at(i)->topic)
				{
					delete (*entries)->at(i);
					break;
				}
			}
		}

		/* Remove the oldest topic when the list is full for the channel */
		if ((*entries)->size() >= (maxhistory + 1))
			delete (*entries)->at((*entries)->size() - 1);

		/* The below code is doing:
		 * - If source isn't given, try to find string 'user' (could be a UUID)
		 * - get the topic set time so a channel creation doesn't trick us into using current time
		 * - add this entry to the beginning to keep them in chronological order
		 */
		User *u = source ? source : User::Find(user);
		time_t ts = c->ci->last_topic_time ? c->ci->last_topic_time : Anope::CurTime;
		(*entries)->insert((*entries)->begin(), new TopicHistoryEntry(c->ci, topic, u ? u->nick : "unknown", ts));
	}

	void OnChanInfo(CommandSource &source, ChannelInfo *ci, InfoFormatter &info, bool show_all) anope_override
	{
		if (!show_all)
			return;

		if (ci->HasExt("TOPICHISTORY"))
			info.AddOption("Topic history");
	}
};

MODULE_INIT(CSTopicHistory)
