# anope-modules
Third Party modules for Anope that I've created.  
The proper place to view and download these modules is on the [Anope IRC Services Modules Site](https://modules.anope.org/index.php?page=home).  
I'll give a short summary of each module here anyways.

### [cs_set_joinflood](https://modules.anope.org/index.php?page=view&id=279 "View module on the Anope Module Site")
A less restrictive join flood protection, a flood of unregistered users will lock the
channel to registered users only. It will do this either with a channel mode (if available)
or via temporary kick-bans.

### [cs_topichistory](https://modules.anope.org/index.php?page=view&id=281 "View module on the Anope Module Site")
Stores a (config set maximum) number of historical topics for a channel. Allows easy
restoration of a historical topic, especially when the topic is accidently changed.

### [hs_offer](https://modules.anope.org/index.php?page=view&id=284 "View module on the Anope Module Site")
Offer specialized vHosts to users, with substitution arguments available. They can be
permanent or limited time (expiry).

### [m_expirenotice](https://modules.anope.org/index.php?page=view&id=277 "View module on the Anope Module Site")
To send a notice (via email and/or memo) to a user that their registered nickname or
channel is soon to expire and/or expired. Fully configurable with what to send for and
how to send the notices, when to send the pre-expiry notices, and the messages sent.

### m_xlinetoakill
Syncs X-Lines (G, Z, R) from the uplink IRCd to the AKILL list. Works on server sync and as X-Lines are added or removed. Requires OperServ, AKILL, and the InspIRCd 2.0 protocol (aka only works with InspIRCd 2.0).

### [os_expiring](https://modules.anope.org/index.php?page=view&id=276 "View module on the Anope Module Site")
A simple Services Oper command to list out soon to expire nicknames or channels.

### [os_notify](https://modules.anope.org/index.php?page=view&id=283 "View module on the Anope Module Site")
This module allows Opers to be notified of flagged events done by Users matching
a mask (wildcard and regex allowed).

### [os_swhois](https://modules.anope.org/index.php?page=view&id=282 "View module on the Anope Module Site")
A different (and newer) version of OS_SWHOIS; the same configuration can be used from
the other os_swhois module to this one. Provides a fully featured SWHOIS system with
features and syntax similar to the rest of Anope.
