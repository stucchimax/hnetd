/*
 * Author: Pierre Pfister
 *
 * Prefix assignment stable storage API tester.
 *
 */

#include "pa_store.c"

#include <stdio.h>

#include "sput.h"

static struct prefix p1 = {
		.prefix = { .s6_addr = {
				0x20, 0x01, 0x20, 0x01, 0xff, 0xff, 0xff}},
		.plen = 56 };
static struct prefix p2 = {
		.prefix = { .s6_addr = {
				0x20, 0x01, 0x20, 0x02, 0xff, 0xff, 0xff}},
		.plen = 56 };
static struct prefix p1_20 = {
		.prefix = { .s6_addr = {
				0x20, 0x01, 0x20, 0x01, 0xff, 0xff, 0xff, 0x20}},
		.plen = 64 };
static struct prefix p1_21 = {
		.prefix = { .s6_addr = {
				0x20, 0x01, 0x20, 0x01, 0xff, 0xff, 0xff, 0x21}},
		.plen = 64 };
static struct prefix p1_22 = {
		.prefix = { .s6_addr = {
				0x20, 0x01, 0x20, 0x01, 0xff, 0xff, 0xff, 0x22}},
		.plen = 64 };

#define TEST_IFNAME_1 "iface0"
#define TEST_IFNAME_2 "iface1"
#define TEST_PAS_FILE "/tmp/hnetd_pa.db"

static void test_pa_store_init() {
	FILE *f;
	sput_fail_unless(f = fopen(TEST_PAS_FILE, "w"), "Erase test db file");
	if(f)
		fclose(f);
}

static void test_pa_store_basic() {
	struct pa_store *store;
	const struct prefix *res;
	store = pa_store_create(TEST_PAS_FILE);
	sput_fail_unless(store, "Pa storage initialized");
	if(!store)
		return;

	res = pa_store_prefix_get(store, NULL, NULL);
	sput_fail_if(res, "No prefix by now");

	sput_fail_if(pa_store_prefix_add(store, TEST_IFNAME_1, &p1_20), "Adding a prefix");

	res = pa_store_prefix_get(store, NULL, NULL);
	sput_fail_unless(res, "One available");

	sput_fail_if(pas_load(store), "Load from file");
	res = pa_store_prefix_get(store, NULL, NULL);

	sput_fail_if(prefix_cmp(res, &p1_20), "Correct prefix value");
}


int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
	sput_start_testing();
	sput_enter_suite("Prefix assignment stable storage (pa_store.c)"); /* optional */

	openlog("hnetd_test_pa", LOG_PERROR | LOG_PID, LOG_DAEMON);

	sput_run_test(test_pa_store_init);
	sput_run_test(test_pa_store_basic);

	sput_leave_suite(); /* optional */
	sput_finish_testing();
	return sput_get_return_value();
}
