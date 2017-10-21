/*
 * HostServ Offer
 *
 * (C) 2017 - genius3000 (genius3000@g3k.solutions)
 * Please refer to the GPL License in use by Anope at:
 * https://github.com/anope/anope/blob/master/docs/COPYING
 *
 * Offer specialized vHosts to users, with substitution arguments available.
 * They can be permanent or limited time (expiry).
 *
 * Syntax (oper): OFFER {ADD | DEL | LIST | VIEW | CLEAR} +expiry vhost reason | [vhost | entry-num | list]
 * Syntax (user): OFFERLIST [TAKE] [vhost | entry-num | list]
 *
 * Configuration to put into your hostserv config:
module { name = "hs_offer" }
command { service = "HostServ"; name = "OFFER"; command = "hostserv/offer"; permission = "hostserv/offer"; }
command { service = "HostServ"; name = "OFFERLIST"; command = "hostserv/offerlist"; }
 *
 * Don't forget to add 'hostserv/offer' to your oper permissions
 */

/* TODO/Thoughts
 * Test and moar test!
 */

#include "module.h"


struct HostOffer : Serializable
{
 public:
	Anope::string ident;
	Anope::string host;
	Anope::string creator;
	Anope::string reason;
	time_t created;
	time_t expires;

	HostOffer() : Serializable("HostOffer") { }

	HostOffer(const Anope::string &i, const Anope::string &h,  const Anope::string &cr, const Anope::string &r, time_t cd, time_t e)
		: Serializable("HostOffer"), ident(i), host(h), creator(cr), reason(r), created(cd), expires(e) { }

	~HostOffer();

	void Serialize(Serialize::Data &data) const anope_override
	{
		data["ident"] << this->ident;
		data["host"] << this->host;
		data["creator"] << this->creator;
		data["reason"] << this->reason;
		data["created"] << this->created;
		data["expires"] << this->expires;
	}

	static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

class OfferService : public Service
{
 protected:
	Serialize::Checker<std::vector<HostOffer *> > offers;

 public:
	OfferService(Module *m) : Service(m, "OfferService", "offer"), offers("HostOffer") { }

	~OfferService()
	{
		for (unsigned i = offers->size(); i > 0; --i)
			delete (*offers).at(i - 1);
	}

	void Add(HostOffer *ho)
	{
		offers->push_back(ho);
	}

	void Del(HostOffer *ho)
	{
		std::vector<HostOffer *>::iterator it = std::find(offers->begin(), offers->end(), ho);
		if (it != offers->end())
			offers->erase(it);
	}

	void Clear()
	{
		for (unsigned i = offers->size(); i > 0; --i)
			delete (*offers).at(i - 1);
	}

	void Expire(HostOffer *ho)
	{
		Log(Config->GetClient("HostServ"), "expire/offer") << "Expiring vHost Offer " << (!ho->ident.empty() ? ho->ident + "@" : "") << ho->host;
		delete ho;
	}

	HostOffer *Get(unsigned i)
	{
		if (i >= offers->size())
			return NULL;

		HostOffer *ho = offers->at(i);
		if (ho && ho->expires && ho->expires <= Anope::CurTime)
		{
			Expire(ho);
			return NULL;
		}

		return ho;
	}

	HostOffer *FindExact(const Anope::string &match)
	{
		for (unsigned i = offers->size(); i > 0; --i)
		{
			HostOffer *ho = offers->at(i - 1);
			Anope::string vhost = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;

			if (ho->expires && ho->expires <= Anope::CurTime)
				Expire(ho);
			else if (vhost.equals_ci(match))
				return ho;
		}

		return NULL;
	}

	std::vector<HostOffer *> GetAll()
	{
		std::vector<HostOffer *> list;

		for (unsigned i = offers->size(); i > 0; --i)
		{
			HostOffer *ho = offers->at(i - 1);

			if (ho->expires && ho->expires <= Anope::CurTime)
				Expire(ho);
			else
				list.insert(list.begin(), ho);
		}

		return list;
	}

	unsigned GetCount()
	{
		return offers->size();
	}
};

static ServiceReference<OfferService> offer_service("OfferService", "offer");

HostOffer::~HostOffer()
{
	if (offer_service)
		offer_service->Del(this);
}

Serializable* HostOffer::Unserialize(Serializable *obj, Serialize::Data &data)
{
	if (!offer_service)
		return NULL;

	HostOffer *ho;
	if (obj)
		ho = anope_dynamic_static_cast<HostOffer *>(obj);
	else
		ho = new HostOffer;

	data["ident"] >> ho->ident;
	data["host"] >> ho->host;
	data["reason"] >> ho->reason;
	data["creator"] >> ho->creator;
	data["created"] >> ho->created;
	data["expires"] >> ho->expires;

	if (!obj)
		offer_service->Add(ho);

	return ho;
}

class OfferDelCallback : public NumberList
{
	CommandSource &source;
	unsigned deleted;
	Command *cmd;

 public:
	OfferDelCallback(CommandSource &_source, const Anope::string &numlist, Command *c) : NumberList(numlist, true), source(_source), deleted(0), cmd(c) { }

	~OfferDelCallback()
	{
		if (!deleted)
		{
			source.Reply("No matching entries on the Offer list.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		if (deleted == 1)
			source.Reply("Deleted 1 entry from the Offer list.");
		else
			source.Reply("Deleted %d entries from the Offer list.");
	}

	void HandleNumber(unsigned number) anope_override
	{
		if (!number)
			return;

		HostOffer *ho = offer_service->Get(number - 1);
		if (!ho)
			return;

		Log(LOG_ADMIN, source, cmd) << "to remove " << (!ho->ident.empty() ? ho->ident + "@" : "") << ho->host << " from the list";
		delete ho;
		++deleted;
	}
};

/* Common functions */
bool isvalidchar(char c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')
		return true;

	return false;
}

unsigned ValidateIdent(const Anope::string &ident)
{
	if (ident.length() > Config->GetBlock("networkinfo")->Get<unsigned>("userlen"))
		return 1;

	for (Anope::string::const_iterator s = ident.begin(), s_end = ident.end(); s != s_end; ++s)
	{
		if (!isvalidchar(*s))
			return 2;
	}

	return 0;
}

unsigned ValidateHost(const Anope::string &host)
{
	if (host.length() > Config->GetBlock("networkinfo")->Get<unsigned>("hostlen"))
		return 1;
	if (!IRCD->IsHostValid(host))
		return 2;

	return 0;
}

const Anope::string ReplaceArgs(const Anope::string &ih, const Anope::string &nick)
{
	if (ih.empty() || ih.find('$') == Anope::string::npos)
		return ih;

	Anope::string str = ih;
	NickAlias *na = NickAlias::Find(nick);

	str = str.replace_all_ci("$account", (na ? na->nc->display : " "));
	str = str.replace_all_ci("$regepoch", (na ? stringify(na->time_registered) : ""));
	str = str.replace_all_ci("$netname", Config->GetBlock("networkinfo")->Get<Anope::string>("networkname"));

	if (Anope::Match(ih, "*$regdate*"))
	{
		if (!na)
		{
			str = str.replace_all_ci("$regdate", "");
			return str;
		}

		tm tm = *localtime(&(na->time_registered));
		char buf[BUFSIZE];
		strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
		str = str.replace_all_ci("$regdate", buf);
	}

	return str;
}

class CommandHSOffer : public Command
{
 private:
	ServiceReference<OfferService> os;

	void DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (params.size() < 4)
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		Anope::string expiry, ident, host;

		expiry = params[1];
		if (expiry[0] != '+')
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		time_t expires = Anope::DoTime(expiry);
		if (isdigit(expiry[expiry.length() - 1]))
			expires *= 86400;
		if (expires > 0)
			expires += Anope::CurTime;

		const Anope::string &vHost = params[2];
		const Anope::string &reason = params[3];

		size_t at = vHost.find('@');
		if (at == Anope::string::npos)
			host = vHost;
		else
		{
			ident = vHost.substr(0, at);
			host = vHost.substr(at + 1);
		}

		if (host.empty())
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		if (!ident.empty())
		{
			if (!IRCD->CanSetVIdent)
			{
				source.Reply(HOST_NO_VIDENT);
				return;
			}

			Anope::string sub_ident;

			/* Only allow one argument in the ident as $account is likely the only one to not be too long */
			size_t sigil = ident.find('$');
			if (sigil != Anope::string::npos && ident.substr(sigil + 1).find('$') != Anope::string::npos)
			{
				source.Reply("You cannot have more than one argument in the vIdent.");
				return;
			}

			if (sigil != Anope::string::npos)
				sub_ident = ReplaceArgs(ident, (source.GetAccount() ? source.GetAccount()->display : source.GetNick()));
			else
				sub_ident = ident;

			unsigned ret = 0;
			if ((ret = ValidateIdent(sub_ident)) > 0)
			{
				if (ret == 1)
				{
					/* Let's give $account a chance, it might be good for some users */
					if (!Anope::Match(ident, "*$account*"))
					{
						source.Reply(HOST_SET_IDENTTOOLONG, Config->GetBlock("networkinfo")->Get<unsigned>("userlen"));
						return;
					}
				}
				else if (ret == 2)
				{
					source.Reply(HOST_SET_IDENT_ERROR);
					return;
				}
			}
		}

		Anope::string sub_host;
		if (host.find('$') != Anope::string::npos)
			sub_host = ReplaceArgs(host, (source.GetAccount() ? source.GetAccount()->display : source.GetNick()));
		else
			sub_host = host;

		unsigned ret = 0;
		if ((ret = ValidateHost(sub_host)) > 0)
		{
			if (ret == 1)
				source.Reply(HOST_SET_TOOLONG, Config->GetBlock("networkinfo")->Get<unsigned>("hostlen"));
			else if (ret == 2)
				source.Reply(HOST_SET_ERROR);

			return;
		}

		const Anope::string full_vHost = (!ident.empty() ? ident + "@" : "") + host;
		if (os->FindExact(full_vHost))
		{
			source.Reply("Host offer \002%s\002 already exists.", full_vHost.c_str());
			return;
		}

		HostOffer *ho = new HostOffer(ident, host, source.GetNick(), reason, Anope::CurTime, expires);
		os->Add(ho);

		Log(LOG_ADMIN, source, this) << "to add a Host offer of " << full_vHost << " (reason: " << reason << ")";
		source.Reply("\002%s\002 added to the Host offer list.", full_vHost.c_str());
	}

	void DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &match = params.size() > 1 ? params[1] : "";
		if (match.empty())
		{
			this->OnSyntaxError(source, "DEL");
			return;
		}

		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		if (isdigit(match[0]) && match.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			OfferDelCallback list(source, match, this);
			list.Process();
		}
		else
		{
			const HostOffer *ho = os->FindExact(match);
			if (!ho)
			{
				source.Reply("\002%s\002 not found on the Offer list.", match.c_str());
				return;
			}

			if (Anope::ReadOnly)
				source.Reply(READ_ONLY_MODE);

			const Anope::string vHost = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
			Log(LOG_ADMIN, source, this) << "to remove " << vHost << " from the list";
			source.Reply("\002%s\002 deleted from the Offer list.", vHost.c_str());

			delete ho;
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

					const HostOffer *ho = offer_service->Get(number - 1);
					if (!ho)
						return;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
					entry["vHost"] = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
					entry["Reason"] = ho->reason;
					entry["Creator"] = ho->creator;
					entry["Created"] = Anope::strftime(ho->created, source.GetAccount(), true);
					entry["Expires"] = Anope::Expires(ho->expires, source.GetAccount());
					list.AddEntry(entry);
				}
			} nl_list(source, list, match);
			nl_list.Process();
		}
		else
		{
			const std::vector<HostOffer *> &offers = os->GetAll();
			for (unsigned i = 0; i < offers.size(); ++i)
			{
				const HostOffer *ho = offer_service->Get(i);
				if (!ho)
					return;

				const Anope::string vHost = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
				if (match.empty() || match.equals_ci(vHost) || Anope::Match(vHost, match, false, true))
				{
					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["vHost"] = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
					entry["Reason"] = ho->reason;
					entry["Creator"] = ho->creator;
					entry["Created"] = Anope::strftime(ho->created, source.GetAccount(), true);
					entry["Expires"] = Anope::Expires(ho->expires, source.GetAccount());
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply("No matching entries on the Offer list.");
		else
		{
			source.Reply("Current Offer list:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of Offer list.");
		}
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("vHost").AddColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("vHost").AddColumn("Reason");
		list.AddColumn("Creator").AddColumn("Created").AddColumn("Expires");

		this->ProcessList(source, params, list);
	}

	void DoClear(CommandSource &source)
	{
		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		os->Clear();

		Log(LOG_ADMIN, source, this) << "to clear the list";
		source.Reply("Offer list has been cleared.");
	}

 public:
	CommandHSOffer(Module *creator) : Command(creator, "hostserv/offer", 1, 4), os("OfferService", "offer")
	{
		this->SetDesc("Manipulate the vHost Offer list");
		this->SetSyntax("ADD +\037expiry\037 \037vHost\037 \037reason\037");
		this->SetSyntax("DEL {\037vHost\037 | \037entry-num\037 | \037list\037}");
		this->SetSyntax("LIST [\037vHost\037 | \037entry-num\037 | \037list\037");
		this->SetSyntax("VIEW [\037vHost\037 | \037entry-num\037 | \037list\037");
		this->SetSyntax("CLEAR");
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!os)
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
			this->DoClear(source);
		else
			this->OnSyntaxError(source, "");
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("Offer specialized vHosts to your users. These offers can have a set\n"
			     "expiry (limited time only offers) or be permanent. Arguments can be used\n"
			     "to create unique to the user vHosts upon taking.");
		source.Reply(" ");
		source.Reply("The \002ADD\002 command requires all 3 parameters.\n"
			     "\037expiry\037 is specified as an integer followed by one of \037d\37\n"
			     "(days), \037h\037 (hours), or \037m\037 (minutes). Combinations (such as\n"
			     "\0371h30m\037) are not permitted. If a unit specifier is not included,\n"
			     "the default is days (so \037+30\037 by itself means 30 days).\n"
			     "To add an Offer which does not expire, use \037+0\037.");

		if (IRCD->CanSetVIdent)
		{
			source.Reply("vHost can be \037vIdent@vHost\037 or just \037vHost\037\n"
				     "and both can contain arguments for substitution. Note that\n"
				     "the vIdent can only be %d characters long.",
				     Config->GetBlock("networkinfo")->Get<unsigned>("userlen"));
		}
		else
			source.Reply("The \037vHost\037 can contain arguments for substitution.");

		source.Reply("Available arguments are:\n"
			     "$account - Display nick of the user's account\n"
			     "$regepoch - Time registered in epoch time\n"
			     "$regdate - Date registered in YYYY-MM-DD\n"
			     "$netname - Network Name\n"
			     "The \037reason\037 is visible to users.");
		source.Reply(" ");
		source.Reply("The \002DEL\002 command requires a parameter, one of a vHost to match,\n"
			     "an entry number, or a list of entry numbers (1-5 or 1-3,5 format).\n"
			     "The \002LIST\002 and \002VIEW\002 commands can be used with no\n"
			     "parameters or with one of the above parameters, for DEL.\n");
		source.Reply("The \002CLEAR\002 command clears all of the Offers from the list.");

		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		if (subcommand.equals_ci("ADD"))
			source.Reply("ADD +\037expiry\037 \037vHost\037 \037reason\037");
		if (subcommand.equals_ci("DEL"))
			source.Reply("DEL {\037vHost\037 | \037entry-num\037 | \037list\037}");
	}
};

class CommandHSOfferList : public Command
{
 private:
	ServiceReference<OfferService> os;

	void DoTake(CommandSource &source, const std::vector<Anope::string> &params)
	{
		NickAlias *na = NickAlias::Find(source.GetNick());
		if (!na || na->nc != source.GetAccount())
		{
			source.Reply(ACCESS_DENIED);
			return;
		}

		if (source.GetAccount()->HasExt("UNCONFIRMED"))
		{
			source.Reply("You must confirm your account before you may take a vHost.");
			return;
		}

		time_t take_delay = Config->GetModule(this->module)->Get<time_t>("takedelay");
		if (take_delay > 0 && na->GetVhostCreated() + take_delay > Anope::CurTime)
		{
			source.Reply("Please wait %d seconds before taking a new vHost.", take_delay);
			return;
		}

		const Anope::string &match = params.size() > 1 ? params[1] : "";
		if (match.empty())
		{
			this->OnSyntaxError(source, "TAKE");
			return;
		}

		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		HostOffer *ho;

		if (match.find_first_not_of("1234567890") == Anope::string::npos)
		{
			unsigned number = convertTo<unsigned>(match);
			ho = os->Get(number - 1);
			if (!ho)
			{
				source.Reply("%d is an invalid entry number", number);
				return;
			}
		}
		else
		{
			ho = os->FindExact(match);
			if (!ho)
			{
				source.Reply("\002%s\002 not found on the Offer list.", match.c_str());
				return;
			}
		}
		if (Anope::ReadOnly)
			source.Reply(READ_ONLY_MODE);

		const Anope::string &ident = ReplaceArgs(ho->ident, source.GetNick());
		const Anope::string &host = ReplaceArgs(ho->host, source.GetNick());

		unsigned ret = 0;
		if ((ret = ValidateIdent(ident)) > 0)
		{
			if (ret == 1)
				source.Reply(HOST_SET_IDENTTOOLONG, Config->GetBlock("networkinfo")->Get<unsigned>("userlen"));
			else if (ret == 2)
				source.Reply(HOST_SET_IDENT_ERROR);

			return;
		}

		if ((ret = ValidateHost(host)) > 0)
		{
			if (ret == 1)
				source.Reply(HOST_SET_TOOLONG, Config->GetBlock("networkinfo")->Get<unsigned>("hostlen"));
			else if (ret == 2)
				source.Reply(HOST_SET_ERROR);

			return;
		}

		const Anope::string full_vHost = (!ident.empty() ? ident + "@" : "") + host;

		Log(LOG_COMMAND, source, this) << "to take offer " << (!ho->ident.empty() ? ho->ident + "@" : "") << ho->host << " and set their vHost to " << full_vHost;
		source.Reply("Your vHost has been set to %s.", full_vHost.c_str());

		na->SetVhost(ident, host, ho->creator);
		FOREACH_MOD(OnSetVhost, (na));
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		NickAlias *na = NickAlias::Find(source.GetNick());
		if (!na || na->nc != source.GetAccount())
		{
			source.Reply(ACCESS_DENIED);
			return;
		}

		if (source.GetAccount()->HasExt("UNCONFIRMED"))
		{
			source.Reply("You must confirm your account before you can view the offer list.");
			return;
		}

		if (os->GetCount() == 0)
		{
			source.Reply("Offer list is empty.");
			return;
		}

		ListFormatter list(source.GetAccount());
		list.AddColumn("Number").AddColumn("Offer vHost").AddColumn("Your vHost").AddColumn("Expires").AddColumn("Reason");

		const Anope::string &match = params.size() > 0 ? params[0] : "";
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

					const HostOffer *ho = offer_service->Get(number - 1);
					if (!ho)
						return;

					const Anope::string &ident = ReplaceArgs(ho->ident, source.GetNick());
					const Anope::string &host = ReplaceArgs(ho->host, source.GetNick());
					unsigned ret_ident = ValidateIdent(ident);
					unsigned ret_host = ValidateHost(host);
					bool invalid = (ret_ident || ret_host);

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
					entry["Offer vHost"] = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
					entry["Your vHost"] = (!ident.empty() ? ident + "@" : "") + host + (invalid ? " (Invalid)" : "");
					entry["Expires"] = Anope::Expires(ho->expires, source.GetAccount());
					entry["Reason"] = ho->reason;
					list.AddEntry(entry);
				}
			} nl_list(source, list, match);
			nl_list.Process();
		}
		else
		{
			const std::vector<HostOffer *> &offers = os->GetAll();
			for (unsigned i = 0; i < offers.size(); ++i)
			{
				const HostOffer *ho = offers.at(i);
				if (!ho)
					continue;

				const Anope::string vHost = (!ho->ident.empty() ? ho->ident + "@" : "") + ho->host;
				if (match.empty() || match.equals_ci(vHost) || Anope::Match(vHost, match, false, true))
				{
					const Anope::string &ident = ReplaceArgs(ho->ident, source.GetNick());
					const Anope::string &host = ReplaceArgs(ho->host, source.GetNick());
					unsigned ret_ident = ValidateIdent(ident);
					unsigned ret_host = ValidateHost(host);
					bool invalid = (ret_ident || ret_host);

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["Offer vHost"] = vHost;
					entry["Your vHost"] = (!ident.empty() ? ident + "@" : "") + host + (invalid ? " (Invalid)" : "");
					entry["Expires"] = Anope::Expires(ho->expires, source.GetAccount());
					entry["Reason"] = ho->reason;
					list.AddEntry(entry);
				}
			}
		}

		if (list.IsEmpty())
			source.Reply("No matching entries on the offer list.");
		else
		{
			source.Reply("Current offer list:");

			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply("End of offer list.");
		}
	}

 public:
	CommandHSOfferList(Module *creator) : Command(creator, "hostserv/offerlist", 0, 2), os("OfferService", "offer")
	{
		this->SetDesc("List or take a vHost from the Offer list");
		this->SetSyntax(" [\037vHost mask\037 | \037entry-num\037 | \037list\037]");
		this->SetSyntax("TAKE {\037vHost\037 | \037entry-num\037}");
		this->RequireUser(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!os)
			return;

		if (!params.empty() && params[0].equals_ci("TAKE"))
			this->DoTake(source, params);
		else
			this->DoList(source, params);
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply("List or take an offered vHost.");
		source.Reply("With no parameters a complete list is shown. You can filter that with\n"
			     "a wildcard \037user@host\037 or \037host\037 mask, an entry-number,\n"
			     "or a list (1-5 or 1-3,5 format).");
		source.Reply("The \002TAKE\002 command requires either the exact \037Offer vHost\037 as\n"
			     "shown or the entry-number.");

		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		if (subcommand.equals_ci("TAKE"))
			source.Reply("TAKE {\037vHost\037 | \037entry-num\037}");
	}
};

class HSOffer : public Module
{
	OfferService offerService;
	Serialize::Type hostoffer_type;
	CommandHSOffer commandhsoffer;
	CommandHSOfferList commandhsofferlist;

 public:
	HSOffer(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
		offerService(this), hostoffer_type("HostOffer", HostOffer::Unserialize),
		commandhsoffer(this), commandhsofferlist(this)
	{
		if (Anope::VersionMajor() != 2 || Anope::VersionMinor() != 0)
			throw ModuleException("Requires version 2.0.x of Anope.");

		this->SetAuthor("genius3000");
		this->SetVersion("0.5.0");
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		if (!conf->GetClient("HostServ"))
			throw ModuleException("Requires HostServ to be loaded.");
	}
};

MODULE_INIT(HSOffer)
