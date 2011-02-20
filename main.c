/* main.c - rpgserv main() routine.
 * Idea completely ripped off from irc.darkmyst.org rpgserv, but theirs is
 * closed-source, so...
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"rpgserv/main", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

static void rs_cmd_help(sourceinfo_t *si, int parc, char *parv[]);
static void rs_cmd_enable(sourceinfo_t *si, int parc, char *parv[]);
static void rs_cmd_disable(sourceinfo_t *si, int parc, char *parv[]);
static void rs_cmd_set(sourceinfo_t *si, int parc, char *parv[]);
static void rs_cmd_list(sourceinfo_t *si, int parc, char *parv[]);
static void rs_cmd_info(sourceinfo_t *si, int parc, char *parv[]);

command_t rs_help = { "HELP", N_("Displays contextual help information."),
                      AC_NONE, 2, rs_cmd_help, { .path = "help" } };
command_t rs_enable = { "ENABLE", N_("Enable RPGServ for a channel."),
                        PRIV_HELPER, 1, rs_cmd_enable, { .path = "rpgserv/enable" } };
command_t rs_disable = { "DISABLE", N_("Disable RPGServ for a channel."),
                         PRIV_HELPER, 1, rs_cmd_disable, { .path = "rpgserv/disable" } };
command_t rs_set = { "SET", N_("Sets RPG properties of your channel."),
                     AC_NONE, 3, rs_cmd_set, { .path = "rpgserv/set" } };
command_t rs_list = { "LIST", N_("Lists games."),
                      AC_NONE, 0, rs_cmd_list, { .path = "rpgserv/list" } };
command_t rs_info = { "INFO", N_("Displays info for a particular game."),
                      AC_NONE, 1, rs_cmd_info, { .path = "rpgserv/info" } };

service_t *rpgserv;
mowgli_list_t rs_conftable;

static void rs_cmd_help(sourceinfo_t *si, int parc, char *parv[])
{
	char *command = parv[0];
	if (!command)
	{
		command_success_nodata(si, _("***** \2%s Help\2 *****"), si->service->nick);
		command_success_nodata(si, _("\2%s\2 allows users to search for game channels by matching on properties."), si->service->nick);
		command_success_nodata(si, " ");
		command_success_nodata(si, _("For more information on a command, type:"));
		command_success_nodata(si, "\2/%s%s help <command>\2", (ircd->uses_rcommand == false) ? "msg " : "", rpgserv->disp);
		command_success_nodata(si, " ");
		command_help(si, si->service->commands);
		command_success_nodata(si, _("***** \2End of Help\2 *****"));
		return;
	}

	help_display(si, si->service, command, si->service->commands);
}

static void rs_cmd_enable(sourceinfo_t *si, int parc, char *parv[])
{
	char *chan = parv[0];
	mychan_t *mc;

	if (!chan)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "ENABLE");
		command_fail(si, fault_needmoreparams, _("Syntax: ENABLE <channel>"));
		return;
	}

	mc = mychan_find(chan);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), chan);
		return;
	}

	if (metadata_find(mc, "private:rpgserv:enabled"))
	{
		command_fail(si, fault_nochange, _("\2%s\2 already has RPGServ enabled."), chan);
		return;
	}

	metadata_add(mc, "private:rpgserv:enabled", si->su->nick);
	logcommand(si, CMDLOG_ADMIN, "RPGSERV:ENABLE: \2%s\2", chan);
	command_success_nodata(si, _("RPGServ enabled for \2%s\2."), chan);
}

static void rs_cmd_disable(sourceinfo_t *si, int parc, char *parv[])
{
	char *chan = parv[0];
	mychan_t *mc;

	if (!chan)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DISABLE");
		command_fail(si, fault_needmoreparams, _("Syntax: DISABLE <channel>"));
		return;
	}

	mc = mychan_find(chan);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), chan);
		return;
	}

	if (!metadata_find(mc, "private:rpgserv:enabled"))
	{
		command_fail(si, fault_nochange, _("\2%s\2 already has RPGServ disabled."), chan);
		return;
	}

	metadata_delete(mc, "private:rpgserv:enabled");
	logcommand(si, CMDLOG_ADMIN, "RPGSERV:DISABLE: \2%s\2", chan);
	command_success_nodata(si, _("RPGServ disabled for \2%s\2."), chan);
}

static void setting_clear(sourceinfo_t *si, mychan_t *mc, char *setting)
{
	char nbuf[64];
	snprintf(nbuf, sizeof(nbuf), "private:rpgserv:%s", setting);
	if (!metadata_find(mc, nbuf)) {
		command_fail(si, fault_nochange, _("\2%s\2 has no %s."), mc->name, setting);
		return;
	}
	metadata_delete(mc, nbuf);
	command_success_nodata(si, _("Setting \2%s\2 cleared for \2%s\2."), setting, mc->name);
}

static int inlist(const char *needle, const char **haystack)
{
	int i;
	for (i = 0; haystack[i]; i++)
		if (!strcasecmp(needle, haystack[i]))
			return i;
	return -1;
}

static void set_genre(sourceinfo_t *si, mychan_t *mc, char *value)
{
	static const char *genres[] = {
		"apocalypse", "anime", "anthromorph", "cyberpunk",
		"fantasy", "horror", "multigenre", "realistic",
		"scifi", "steampunk", "other", NULL
	};

	if (inlist(value, genres) < 0) {
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid genre."), value);
		return;
	}

	metadata_add(mc, "private:rpgserv:genre", value);
	command_success_nodata(si, _("Genre for \2%s\2 set to \2%s\2."), mc->name, value);
}

static void set_period(sourceinfo_t *si, mychan_t *mc, char *value)
{
	static const char *periods[] = {
		"prehistoric", "antiquity", "middleages", "earlymodern",
		"modern", "future", NULL
	};

	if (inlist(value, periods) < 0) {
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid period."), value);
		return;
	}

	metadata_add(mc, "private:rpgserv:period", value);
	command_success_nodata(si, _("Period for \2%s\2 set to \2%s\2."), mc->name, value);
}

static void set_ruleset(sourceinfo_t *si, mychan_t *mc, char *value)
{
	static const char *rulesets[] = {
		"adnd", "homebrew", "dnd3.0", "dnd3.5", "dnd4.0", "freeform",
		"other", "owod", "nwod", NULL
	};

	if (inlist(value, rulesets) < 0) {
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid ruleset."), value);
		return;
	}

	metadata_add(mc, "private:rpgserv:ruleset", value);
	command_success_nodata(si, _("Ruleset for \2%s\2 set to \2%s\2."), mc->name, value);
}

static void set_rating(sourceinfo_t *si, mychan_t *mc, char *value)
{
	static const char *ratings[] = {
		"g", "pg", "pg13", "r", "adult", NULL
	};

	if (inlist(value, ratings) < 0) {
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid rating."), value);
		return;
	}

	metadata_add(mc, "private:rpgserv:rating", value);
	command_success_nodata(si, _("Rating for \2%s\2 set to \2%s\2."), mc->name, value);
}

static void set_system(sourceinfo_t *si, mychan_t *mc, char *value)
{
	char copy[512];
	char *sp = NULL, *t = NULL;
	static const char *systems[] = {
		"charapproval", "diced", "sheeted"
	};

	strlcpy(copy, value, sizeof(copy));
	t = strtok_r(copy, " ", &sp);
	while (t) {
		if (inlist(t, systems) < 0) {
			command_fail(si, fault_badparams, _("\2%s\2 is not a valid system."), t);
			return;
		}
		t = strtok_r(NULL, " ", &sp);
	}

	metadata_add(mc, "private:rpgserv:system", value);
	command_success_nodata(si, _("system for \2%s\2 set to \2%s\2."), mc->name, value);
}

static void set_setting(sourceinfo_t *si, mychan_t *mc, char *value)
{
	metadata_add(mc, "private:rpgserv:setting", value);
	command_success_nodata(si, _("Setting for \2%s\2 set."), mc->name);
}

static void set_storyline(sourceinfo_t *si, mychan_t *mc, char *value)
{
	metadata_add(mc, "private:rpgserv:storyline", value);
	command_success_nodata(si, _("Storyline for \2%s\2 set."), mc->name);
}

static void set_summary(sourceinfo_t *si, mychan_t *mc, char *value)
{
	metadata_add(mc, "private:rpgserv:summary", value);
	command_success_nodata(si, _("Summary for \2%s\2 set."), mc->name);
}

static struct {
	char *name;
	void (*func)(sourceinfo_t *si, mychan_t *mc, char *value);
} settings[] = {
	{ "genre", set_genre },
	{ "period", set_period },
	{ "ruleset", set_ruleset },
	{ "rating", set_rating },
	{ "system", set_system },
	{ "setting", set_setting },
	{ "storyline", set_storyline },
	{ "summary", set_summary },
	{ NULL, NULL },
};

static void rs_cmd_set(sourceinfo_t *si, int parc, char *parv[])
{
	char *chan;
	char *setting;
	char *value = NULL;
	mychan_t *mc;
	chanacs_t *ca;
	int i;
	char nbuf[64];

	if (parc < 2)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "SET");
		command_fail(si, fault_needmoreparams, _("Syntax: SET <channel> <property> [value...]"));
		return;
	}

	chan = parv[0];
	setting = parv[1];
	if (parc > 2)
		value = parv[2];

	mc = mychan_find(chan);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), chan);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_SET))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	if (!metadata_find(mc, "private:rpgserv:enabled"))
	{
		command_fail(si, fault_noprivs, _("Channel \2%s\2 does not have RPGServ enabled."), chan);
		return;
	}

	for (i = 0; settings[i].name; i++) {
		if (!strcasecmp(settings[i].name, setting)) {
			if (value)
				settings[i].func(si, mc, value);
			else
				setting_clear(si, mc, setting);
			break;
		}
	}

	if (!settings[i].name) {
		command_fail(si, fault_badparams, _("No such setting \2%s\2."), setting);
	}
}

static void rs_cmd_list(sourceinfo_t *si, int parc, char *parv[])
{
	mowgli_patricia_iteration_state_t state;
	mychan_t *mc;
	unsigned int listed = 0;
	char *desc;

	MOWGLI_PATRICIA_FOREACH(mc, &state, mclist)
	{
		if (!metadata_find(mc, "private:rpgserv:enabled"))
			continue;
		if (!metadata_find(mc, "private:rpgserv:summary"))
			desc = "<no summary>";
		else
			desc = metadata_find(mc, "private:rpgserv:summary")->value;
		command_success_nodata(si, "\2%s\2: %s", mc->name, desc);
		listed++;
	}
	command_success_nodata(si, "Listed \2%d\2 channels.", listed);
}

static void rs_cmd_info(sourceinfo_t *si, int parc, char *parv[])
{
	mychan_t *mc;
	metadata_t *md;

	if (parc < 1)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "INFO");
		command_fail(si, fault_needmoreparams, _("Syntax: INFO <channel>"));
		return;
	}

	mc = mychan_find(parv[0]);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), parv[0]);
		return;
	}

	if (!metadata_find(mc, "private:rpgserv:enabled"))
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 does not have RPGServ enabled."), parv[0]);
		return;
	}

	command_success_nodata(si, _("Channel \2%s\2:"), parv[0]);
	md = metadata_find(mc, "private:rpgserv:genre");
	command_success_nodata(si, _("Genre: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:period");
	command_success_nodata(si, _("Period: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:ruleset");
	command_success_nodata(si, _("Ruleset: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:rating");
	command_success_nodata(si, _("Rating: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:system");
	command_success_nodata(si, _("System: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:setting");
	command_success_nodata(si, _("Setting: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:storyline");
	command_success_nodata(si, _("Storyline: %s"), md ? md->value : "<none>");
	md = metadata_find(mc, "private:rpgserv:summary");
	command_success_nodata(si, _("Summary: %s"), md ? md->value : "<none>");	
}

void _modinit(module_t *m)
{
	rpgserv = service_add("rpgserv", NULL, &rs_conftable);
	service_bind_command(rpgserv, &rs_help);
	service_bind_command(rpgserv, &rs_enable);
	service_bind_command(rpgserv, &rs_disable);
	service_bind_command(rpgserv, &rs_set);
	service_bind_command(rpgserv, &rs_list);
	service_bind_command(rpgserv, &rs_info);
}

void _moddeinit(module_unload_intent_t intent)
{
	if (rpgserv) {
		service_delete(rpgserv);
		rpgserv = NULL;
	}

	service_unbind_command(rpgserv, &rs_help);
	service_unbind_command(rpgserv, &rs_enable);
	service_unbind_command(rpgserv, &rs_disable);
	service_unbind_command(rpgserv, &rs_set);
	service_unbind_command(rpgserv, &rs_list);
	service_unbind_command(rpgserv, &rs_info);
}
