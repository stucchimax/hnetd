#include "hnetd.h"
#include "sput.h"
#include "smock.h"

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "fake_uloop.h"
#include "prefix_utils.c"
#include "iface.c"

void pa_data_subscribe(__unused struct pa_data *data, __unused struct pa_data_user *user) {}
struct pa_iface* pa_iface_get(__unused struct pa_data *d, __unused const char *ifname, __unused bool goc){ return NULL; }
void pa_core_static_prefix_init(__unused struct pa_static_prefix_rule *rule, __unused const char *ifname,
		__unused const struct prefix* p, __unused bool hard) {};
void pa_core_rule_add(__unused struct pa_core *core, __unused struct pa_rule *rule) {};
void platform_set_owner(__unused struct iface *c, __unused bool enable) {}
int platform_init(__unused struct pa_data *data, __unused const char *pd_socket) { return 0; }
void platform_set_address(__unused struct iface *c, __unused struct iface_addr *addr, __unused bool enable) {}
void platform_set_route(__unused struct iface *c, __unused struct iface_route *addr, __unused bool enable) {}
void platform_iface_free(__unused struct iface *c) {}
void platform_set_internal(__unused struct iface *c, __unused bool internal) {}
void platform_filter_prefix(__unused struct iface *c, __unused const struct prefix *p, __unused bool enable) {}
void platform_iface_new(__unused struct iface *c, __unused const char *handle) { c->platform = (void*)1; }
void platform_set_dhcpv6_send(__unused struct iface *c, __unused const void *dhcpv6_data, __unused size_t len,
		__unused const void *dhcp_data, __unused size_t len4) {}
void platform_set_prefix_route(__unused const struct prefix *p, __unused bool enable) {}
void platform_restart_dhcpv4(__unused struct iface *c) {}

void intiface_mock(__unused struct iface_user *u, __unused const char *ifname, bool enabled)
{
	smock_push_bool(ifname, enabled);
	uloop_end();
}

void extdata_mock(__unused struct iface_user *u, __unused const char *ifname, __unused const void *dhcpv6_data, size_t dhcpv6_len)
{
	if (dhcpv6_len)
		smock_push_int("extdata", dhcpv6_len);
}

void prefix_mock(__unused struct iface_user *u, __unused const char *ifname,
		__unused const struct prefix *prefix, __unused const struct prefix *excluded,
		hnetd_time_t valid_until, __unused hnetd_time_t preferred_until,
		__unused const void *dhcpv6_data, __unused size_t dhcpv6_len)
{
	if (valid_until > hnetd_time()) {
		smock_push("prefix_prefix", (void*)prefix);
		smock_push_int64("prefix_valid", valid_until);
		smock_push_int64("prefix_preferred", preferred_until);
		smock_push("dhcpv6_data", (void*)dhcpv6_data);
		smock_push_int("dhcpv6_len", dhcpv6_len);
	} else {
		smock_push_bool("prefix_remove", true);
	}
}

struct iface_user user_mock = {
	.cb_intiface = intiface_mock,
	.cb_extdata = extdata_mock,
	.cb_prefix = prefix_mock
};


void iface_test_new_unmanaged(void)
{
	iface_register_user(&user_mock);

	struct iface *iface = iface_create("test0", NULL, 0);
	sput_fail_unless(!!iface, "alloc unmanaged");

	struct iface *iface2 = iface_get("test0");
	sput_fail_unless(iface == iface2, "get after create");

	struct iface *iface3 = iface_create("test0", NULL, 0);
	sput_fail_unless(iface == iface3, "create after create");

	iface_remove(iface);
	sput_fail_unless(!iface_get("test0"), "delete");

	smock_is_empty();
	iface_unregister_user(&user_mock);
}


void iface_test_new_managed(void)
{
	iface_register_user(&user_mock);
	struct prefix p = {IN6ADDR_LOOPBACK_INIT, 0};
	char test[] = "test";

	struct iface *iface00 = iface_create("test00", "test00", 0);
	iface_update_ipv4_uplink(iface00);
	iface_set_ipv4_uplink(iface00);
	iface_commit_ipv4_uplink(iface00);
	smock_pull_bool_is("test00", false);

	struct iface *iface = iface_create("test0", "test0", 0);
	iface->carrier = true;
	iface_discover_border(iface);

	sput_fail_unless(!!iface, "alloc managed");

	struct iface *iface2 = iface_get("test0");
	sput_fail_unless(iface == iface2, "get after create");

	struct iface *iface3 = iface_create("test0", "test0", 0);
	sput_fail_unless(iface == iface3, "create after create");

	smock_pull_bool_is("test0", false);

	uloop_cancelled = false;
	uloop_run();
	smock_pull_bool_is("test0", true);

	iface_update_ipv4_uplink(iface);
	iface_set_ipv4_uplink(iface);
	iface_commit_ipv4_uplink(iface);
	smock_pull_bool_is("test0", false);

	iface_update_ipv4_uplink(iface);
	iface_commit_ipv4_uplink(iface);
	uloop_cancelled = false;
	uloop_run();
	smock_pull_bool_is("test0", true);

	iface_update_ipv6_uplink(iface);
	iface_add_delegated(iface, &p, NULL, HNETD_TIME_MAX, 0, test, sizeof(test));
	iface_commit_ipv6_uplink(iface);

	smock_pull_bool_is("test0", false);
	sput_fail_unless(!prefix_cmp(&p, smock_pull("prefix_prefix")), "prefix address");
	smock_pull_int64_is("prefix_valid", HNETD_TIME_MAX);
	smock_pull_int64_is("prefix_preferred", 0);
	sput_fail_unless(!strcmp(smock_pull("dhcpv6_data"), "test"), "dhcpv6_data");
	smock_pull_int_is("dhcpv6_len", sizeof(test));

	iface_update_ipv4_uplink(iface);
	iface_set_ipv4_uplink(iface);
	iface_commit_ipv4_uplink(iface);

	iface_update_ipv6_uplink(iface);
	iface_commit_ipv6_uplink(iface);
	smock_pull_bool_is("prefix_remove", true);

	iface_update_ipv4_uplink(iface);
	iface_commit_ipv4_uplink(iface);

	uloop_cancelled = false;
	uloop_run();
	smock_pull_bool_is("test0", true);

	iface_remove(iface);
	sput_fail_unless(!iface_get("test0"), "delete");
	smock_pull_bool_is("test0", false);
	smock_is_empty();
	iface_unregister_user(&user_mock);
}


int main()
{
	sput_start_testing();
	sput_enter_suite("iface");
	sput_run_test(iface_test_new_unmanaged);
	sput_run_test(iface_test_new_managed);
	sput_leave_suite();
	sput_finish_testing();
	return sput_get_return_value();
}
