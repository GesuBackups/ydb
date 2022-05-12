Notes:
  In this directory, generated files are given .lst extension, other files
  are given other extensions.
  Files are generated from other files in this directory by mk-generated.sh
  Generated files must also be committed into CVS.
Warning:
  It is a reserved possibility that zone/area files may contain 3+ - level domains

** Domain lists **:
2ld.list - list of domains whose subdomains are considered different
         sites in search results (thus being here is something 'positive' for domains)
         Previous location is /Berkanavt/catalog (aka yweb/pornofilter)
         It is not generated, contains all domains from b2ld.list, d2ld.list,
	 g2ld.list and r2ld.list except those in no-serp-2ld.list

b2ld.list - list of free hostings (-: d=0, etc.; +: sometimes different owners for PR/antispam)

d2ld.list - list of non-free hostings or geo-hostings

g2ld.list - list of geographical or generic domains (+: different owners for PR/antispam)
         Previous location is /hol/pagerank (aka yweb/webutil/pagerank/scripts)
         Temporary location is /Berkanavt/catalog

r2ld.list - list of non-free geographical or generic domains


reg-free.txt - free regional domains (other, russian). usually manual registration via e-mail
relcom-free.txt - free regional domains from Relcom. auto registration via e-mail
ripn-free.txt - free domain zones from RIPN. auto registration via e-mail

** Generaged files **:

hostings.lst - sub-domain hostings (no ns delegation for subdomains)
freezones.lst - free ns delegation for subdomains

j-owners-spm.lst - joined list of domains that antispam system should
         treat as subdomains for different owners.

nsareas.lst - domains whose subdomains are delegated and thus subject of ns scan;
         file is bigger than freezones.lst

** Additional files **:

vragi_mails.txt - whois e-mails, ban in Filter automatically

xrdr.excl - host names to which redirects are allowed (mostly redirect from idiotic providers' 403/404 pages)
xrdr-excl.lst - generated from xrdr.excl

