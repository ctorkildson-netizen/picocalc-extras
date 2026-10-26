/* btkbd.c -- a Bluetooth keyboard for the PicoCalc (Pico 2 W): BTstack's
 * classic HID host, with the keys turned into PicoCalc key events.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson.
 *
 * Built on BTstack's hid_host_demo.c (BlueKitchen), which is free to use on
 * the Raspberry Pi Pico W and Pico 2 W under Raspberry Pi's licence. */
#include "btkbd.h"

#include <stdio.h>
#include <string.h>
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_run_loop_async_context.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "drivers/fat32.h"
#include "drivers/keyboard.h"

/* ------------------------------------------------------------ storage */

/* A few tagged records (BTstack's link keys, and which keyboard is ours),
   kept in RAM and saved whole to the SD card when one changes. The file is
   always the same size, so it's rewritten in place. */
#define TLV_MAX 16
#define TLV_DATA 48
#define STORE_MAGIC 0x314B4250u		/* "PBK1" */
#define STORE_DIR "/bluetooth"
#define STORE_PATH "/bluetooth/pairing.dat"
#define TAG_KEYBOARD 0x4B424430u	/* "KBD0": the paired keyboard's address and name */

typedef struct { uint32_t tag; uint32_t len; uint8_t data[TLV_DATA]; } record_t;
static struct { uint32_t magic; record_t r[TLV_MAX]; } store;

static void store_load(void)
{
	fat32_file_t f;
	size_t got = 0;
	memset(&store, 0, sizeof store);
	if (fat32_open(&f, STORE_PATH) == FAT32_OK) {
		fat32_read(&f, &store, sizeof store, &got);
		fat32_close(&f);
	}
	if (got != sizeof store || store.magic != STORE_MAGIC) {
		memset(&store, 0, sizeof store);
		store.magic = STORE_MAGIC;
	}
}

static bool store_dirty;
static absolute_time_t save_after;

/* saving can wait a moment: the card is slow, and a pairing in progress
   shouldn't be kept waiting for it */
static void store_changed(void)
{
	store_dirty = true;
	save_after = make_timeout_time_ms(3000);
}

static void store_save(void)
{
	store_dirty = false;
	fat32_file_t f;
	if (fat32_open(&f, STORE_PATH) != FAT32_OK) {
		fat32_file_t d;
		if (fat32_dir_create(&d, STORE_DIR) == FAT32_OK) fat32_close(&d);
		if (fat32_create(&f, STORE_PATH) != FAT32_OK) return;
	}
	size_t put;
	fat32_seek(&f, 0);
	fat32_write(&f, &store, sizeof store, &put);
	fat32_close(&f);
}

static record_t *find(uint32_t tag)
{
	for (int i = 0; i < TLV_MAX; i++)
		if (store.r[i].tag == tag) return &store.r[i];
	return NULL;
}

static int tlv_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size)
{
	(void)ctx;
	record_t *r = find(tag);
	if (!r) return 0;
	uint32_t n = r->len < size ? r->len : size;
	if (buf) memcpy(buf, r->data, n);
	return (int)r->len;
}

static int tlv_store(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size)
{
	(void)ctx;
	if (size > TLV_DATA || tag == 0) return 1;
	record_t *r = find(tag);
	if (!r) r = find(0);
	if (!r) return 1;
	r->tag = tag;
	r->len = size;
	memcpy(r->data, data, size);
	store_changed();
	return 0;
}

static void tlv_delete(void *ctx, uint32_t tag)
{
	(void)ctx;
	record_t *r = find(tag);
	if (!r) return;
	memset(r, 0, sizeof *r);
	store_changed();
}

static const btstack_tlv_t tlv = { tlv_get, tlv_store, tlv_delete };

/* The SDK's own version of this keeps pairings in the last sectors of flash,
   which the PicoCalc's UF2 Loader and other programs may use; this one keeps
   them on the SD card instead. cyw43_arch_init() calls it. */
bool btstack_cyw43_init(async_context_t *context)
{
	btstack_memory_init();
	btstack_run_loop_init(btstack_run_loop_async_context_get_instance(context));
	hci_init(hci_transport_cyw43_instance(), NULL);
	store_load();
	btstack_tlv_set_instance(&tlv, NULL);
	hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(&tlv, NULL));
	return true;
}

void btstack_cyw43_deinit(async_context_t *context)
{
	(void)context;
	hci_power_control(HCI_POWER_OFF);
	hci_close();
	btstack_run_loop_async_context_deinit();
	btstack_run_loop_deinit();
	btstack_memory_deinit();
}

/* ------------------------------------------------------------ state */

static btkbd_state_t state = BTKBD_OFF;
static char status[48] = "Bluetooth off";
static char code[12];			/* what to type while pairing */

static bool paired;
static bd_addr_t paired_addr;
static char paired_name[TLV_DATA - 6 + 1];

static uint16_t cid;			/* the HID connection, or 0 */
static bool descriptor_ok;
static uint8_t descriptor[512];
static btstack_packet_callback_registration_t hci_cb;
static btstack_timer_source_t retry_timer;
#define RETRY_MS 15000

#define FOUND_MAX 8
static struct {
	bd_addr_t addr;
	uint8_t psrm;
	uint16_t clock;
	bool named;
	char name[32];
} found[FOUND_MAX];
static int n_found, naming = -1, pairing_with = -1, pair_after_search = -1;
static btstack_timer_source_t name_timer;
#define NAME_MS 5000

/* for the screen, while this is being tried out */
static uint8_t last_event;
static int d_conn = -1, d_pair = -1, d_auth = -1, d_enc = -1, d_disc = -1, d_io = -1, d_hid = -1;
#define TRACE 12
static uint8_t trace[TRACE];
static int n_trace;
static void note_err(const char *where, uint8_t err) { (void)where; d_hid = err; }
static void hx(char *b, size_t n, const char *k, int v)
{
	size_t l = strlen(b);
	if (v < 0) snprintf(b + l, n - l, "%s-- ", k);
	else snprintf(b + l, n - l, "%s%02X ", k, v);
}
const char *btkbd_diag(void)
{
	static char b[64];
	snprintf(b, sizeof b, "st%d ", (int)state);
	hx(b, sizeof b, "con", d_conn); hx(b, sizeof b, "io", d_io); hx(b, sizeof b, "pr", d_pair);
	hx(b, sizeof b, "au", d_auth); hx(b, sizeof b, "en", d_enc); hx(b, sizeof b, "dc", d_disc); hx(b, sizeof b, "hid", d_hid);
	return b;
}
const char *btkbd_trace(void)
{
	static char b[64];
	b[0] = 0;
	for (int i = 0; i < n_trace; i++) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, "%02X ", trace[i]); }
	return b;
}
static void reset_diag(void) { d_conn = d_pair = d_auth = d_enc = d_disc = d_io = d_hid = -1; n_trace = 0; }
static void start_pairing(int i);

static void set_status(const char *s) { snprintf(status, sizeof status, "%s", s); }

static void load_paired(void)
{
	record_t *r = find(TAG_KEYBOARD);
	paired = r && r->len >= 6;
	if (!paired) return;
	memcpy(paired_addr, r->data, 6);
	memset(paired_name, 0, sizeof paired_name);
	memcpy(paired_name, r->data + 6, r->len - 6);
}

static void save_paired(const bd_addr_t addr, const char *name)
{
	uint8_t d[TLV_DATA];
	size_t n = strlen(name);
	if (n > TLV_DATA - 6) n = TLV_DATA - 6;
	memcpy(d, addr, 6);
	memcpy(d + 6, name, n);
	tlv_store(NULL, TAG_KEYBOARD, d, (uint32_t)(6 + n));
	load_paired();
}

/* ------------------------------------------------------------ key events */

#define EQ 64
static volatile uint16_t eq[EQ];		/* filled in the background, emptied by the program */
static volatile unsigned eq_head, eq_tail;
static bool merge = true;

static void put_event(uint8_t st, uint8_t c)
{
	unsigned next = (eq_head + 1) % EQ;
	if (next == eq_tail) return;		/* full: drop it */
	eq[eq_head] = (uint16_t)(st << 8 | c);
	eq_head = next;
}

uint16_t btkbd_read(void)
{
	unsigned t = eq_tail;
	if (t == eq_head) return 0;
	uint16_t e = eq[t];
	eq_tail = (t + 1) % EQ;
	return e;
}

void btkbd_merge(bool on) { merge = on; }

/* Linked with --wrap=sb_read_keyboard (see bluetooth.cmake), every call to
   sb_read_keyboard() comes here: the PicoCalc's own keys first, then the
   Bluetooth keyboard's. So the text starter's keyboard driver, and anything
   else that reads the keyboard, sees both. */
uint16_t __real_sb_read_keyboard(void);
uint16_t __wrap_sb_read_keyboard(void)
{
	uint16_t e = __real_sb_read_keyboard();
	if ((e >> 8) || !merge) return e;
	return btkbd_read();
}

/* HID usage (page 7) to PicoCalc code: letters lower case, the rest as the
   PicoCalc's keyboard sends them; 0 for keys it doesn't have */
static uint8_t code_of(uint8_t u, bool shift)
{
	static const char plain[] = "1234567890", shifted[] = "!@#$%^&*()";
	static const char sym[] = "-=[]\\#;'`,./", sym_s[] = "_+{}|~:\"~<>?";
	if (u >= 0x04 && u <= 0x1D) return (uint8_t)('a' + u - 0x04);
	if (u >= 0x1E && u <= 0x27) return (uint8_t)(shift ? shifted[u - 0x1E] : plain[u - 0x1E]);
	if (u >= 0x2D && u <= 0x38) return (uint8_t)(shift ? sym_s[u - 0x2D] : sym[u - 0x2D]);
	if (u >= 0x3A && u <= 0x43) {		/* F1-F10 */
		static const uint8_t f[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10 };
		return f[u - 0x3A];
	}
	if (u >= 0x59 && u <= 0x62) return (uint8_t)("1234567890"[u - 0x59]);	/* keypad digits */
	switch (u) {
	case 0x28: case 0x58: return KEY_ENTER;
	case 0x29: return KEY_ESC;
	case 0x2A: return KEY_BACKSPACE;
	case 0x2B: return KEY_TAB;
	case 0x2C: return ' ';
	case 0x39: return KEY_CAPS_LOCK;
	case 0x48: return KEY_BREAK;
	case 0x49: return KEY_INSERT;
	case 0x4A: return KEY_HOME;
	case 0x4B: return KEY_PAGE_UP;
	case 0x4C: return KEY_DEL;
	case 0x4D: return KEY_END;
	case 0x4E: return KEY_PAGE_DOWN;
	case 0x4F: return KEY_RIGHT;
	case 0x50: return KEY_LEFT;
	case 0x51: return KEY_DOWN;
	case 0x52: return KEY_UP;
	case 0x54: return '/';
	case 0x55: return '*';
	case 0x56: return '-';
	case 0x57: return '+';
	case 0x63: return '.';
	}
	return 0;
}

/* what's held down, as the PicoCalc codes sent for it */
#define HELD_MAX 6
static uint8_t held_u[HELD_MAX], held_c[HELD_MAX];
static uint8_t mods;			/* HID modifier bits: 0x11 Ctrl, 0x02 left Shift, 0x20 right Shift, 0x44 Alt */

static void set_mods(uint8_t m)
{
	struct { uint8_t bits, code; } map[] = {
		{ 0x11, KEY_MOD_CTRL }, { 0x02, KEY_MOD_SHL }, { 0x20, KEY_MOD_SHR }, { 0x44, KEY_MOD_ALT },
	};
	for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
		bool was = mods & map[i].bits, now = m & map[i].bits;
		if (now != was) put_event(now ? KEY_STATE_PRESSED : KEY_STATE_RELEASED, map[i].code);
	}
	mods = m;
}

static void release_all(void)
{
	for (int i = 0; i < HELD_MAX; i++)
		if (held_u[i]) { if (held_c[i]) put_event(KEY_STATE_RELEASED, held_c[i]); held_u[i] = held_c[i] = 0; }
	set_mods(0);
}

/* a new set of keys held down: releases for the ones let go, presses for new ones */
static void keys_now(uint8_t m, const uint8_t *keys, int n)
{
	set_mods(m);
	bool shift = m & 0x22;
	for (int i = 0; i < HELD_MAX; i++) {
		if (!held_u[i]) continue;
		bool still = false;
		for (int k = 0; k < n; k++) still |= keys[k] == held_u[i];
		if (!still) {
			if (held_c[i]) put_event(KEY_STATE_RELEASED, held_c[i]);
			held_u[i] = held_c[i] = 0;
		}
	}
	for (int k = 0; k < n; k++) {
		uint8_t u = keys[k];
		if (u < 0x04) continue;			/* none, or "too many keys" */
		bool known = false;
		for (int i = 0; i < HELD_MAX; i++) known |= held_u[i] == u;
		if (known) continue;
		for (int i = 0; i < HELD_MAX; i++) {
			if (held_u[i]) continue;
			held_u[i] = u;
			held_c[i] = code_of(u, shift);
			if (held_c[i]) put_event(KEY_STATE_PRESSED, held_c[i]);
			break;
		}
	}
}

static void handle_report(const uint8_t *report, uint16_t len)
{
	if (len < 1 || report[0] != 0xA1 || !descriptor_ok) return;	/* an input report, once we can read it */
	report++, len--;
	btstack_hid_parser_t parser;
	btstack_hid_parser_init(&parser, hid_descriptor_storage_get_descriptor_data(cid),
	                        hid_descriptor_storage_get_descriptor_len(cid), HID_REPORT_TYPE_INPUT, report, len);
	uint8_t m = 0, keys[HELD_MAX];
	int n = 0;
	bool any = false;
	while (btstack_hid_parser_has_more(&parser)) {
		uint16_t page, usage;
		int32_t value;
		btstack_hid_parser_get_field(&parser, &page, &usage, &value);
		if (page != 0x07) continue;		/* only the keyboard page */
		any = true;
		if (usage >= 0xE0 && usage <= 0xE7) { if (value) m |= (uint8_t)(1 << (usage - 0xE0)); continue; }
		if (usage && n < HELD_MAX) keys[n++] = (uint8_t)usage;
	}
	if (any) keys_now(m, keys, n);		/* (a report from another page, e.g. media keys, changes nothing) */
}

/* ------------------------------------------------------------ connecting */

static void try_paired(void)
{
	if (!paired || cid || state == BTKBD_SEARCHING || state == BTKBD_PAIRING) return;
	if (hid_host_connect(paired_addr, HID_PROTOCOL_MODE_REPORT, &cid) != ERROR_CODE_SUCCESS) cid = 0;
}

static void on_retry(btstack_timer_source_t *t)
{
	(void)t;
	if (state == BTKBD_WAITING && !cid) try_paired();
	btstack_run_loop_set_timer(&retry_timer, RETRY_MS);
	btstack_run_loop_add_timer(&retry_timer);
}

static void idle_status(void)
{
	if (paired) {
		state = BTKBD_WAITING;
		snprintf(status, sizeof status, "Waiting for %.24s", paired_name[0] ? paired_name : "the keyboard");
	} else {
		state = BTKBD_IDLE;
		set_status("No keyboard paired");
	}
}

static void on_name_timeout(btstack_timer_source_t *t);

static void name_next(void)
{
	btstack_run_loop_remove_timer(&name_timer);
	if (pair_after_search < 0)
		for (int i = 0; i < n_found; i++)
			if (!found[i].named) {
				naming = i;
				gap_remote_name_request(found[i].addr, found[i].psrm, found[i].clock | 0x8000);
				btstack_run_loop_set_timer_handler(&name_timer, on_name_timeout);
				btstack_run_loop_set_timer(&name_timer, NAME_MS);
				btstack_run_loop_add_timer(&name_timer);
				return;
			}
	naming = -1;
	idle_status();
	snprintf(status, sizeof status, n_found ? "Found %d: choose one" : "No keyboards found", n_found);
	if (pair_after_search >= 0) { int i = pair_after_search; pair_after_search = -1; start_pairing(i); }
}

static void on_name_timeout(btstack_timer_source_t *t)
{
	(void)t;
	if (naming >= 0) {		/* no answer: call it by its address */
		snprintf(found[naming].name, sizeof found[naming].name, "%s", bd_addr_to_str(found[naming].addr));
		found[naming].named = true;
		name_next();
	}
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	(void)channel; (void)size;
	if (type != HCI_EVENT_PACKET) return;
	bd_addr_t addr;
	uint8_t ev = hci_event_packet_get_type(packet);
	if (ev != HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS && ev != HCI_EVENT_TRANSPORT_PACKET_SENT
	    && ev != HCI_EVENT_COMMAND_COMPLETE && ev != HCI_EVENT_COMMAND_STATUS) {
		last_event = ev;
		uint8_t t = ev == HCI_EVENT_HID_META ? (uint8_t)(0xF0 | (hci_event_hid_meta_get_subevent_code(packet) & 0x0F)) : ev;
		if (n_trace == TRACE) { memmove(trace, trace + 1, TRACE - 1); n_trace--; }
		trace[n_trace++] = t;
	}
	switch (ev) {
	case HCI_EVENT_CONNECTION_COMPLETE: d_conn = hci_event_connection_complete_get_status(packet); break;
	case HCI_EVENT_DISCONNECTION_COMPLETE: d_disc = hci_event_disconnection_complete_get_reason(packet); break;
	case HCI_EVENT_ENCRYPTION_CHANGE: d_enc = hci_event_encryption_change_get_status(packet); break;
	case HCI_EVENT_IO_CAPABILITY_RESPONSE: d_io = hci_event_io_capability_response_get_io_capability(packet); break;
	default: break;
	}
	switch (ev) {
	case BTSTACK_EVENT_STATE:
		if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) break;
		idle_status();
		try_paired();
		break;

	case GAP_EVENT_INQUIRY_RESULT: {
		if (state != BTKBD_SEARCHING || n_found >= FOUND_MAX) break;
		uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
		if (((cod >> 8) & 0x1F) != 0x05) break;		/* not a peripheral */
		gap_event_inquiry_result_get_bd_addr(packet, addr);
		for (int i = 0; i < n_found; i++) if (bd_addr_cmp(found[i].addr, addr) == 0) goto done;
		int i = n_found++;
		bd_addr_copy(found[i].addr, addr);
		found[i].psrm = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
		found[i].clock = gap_event_inquiry_result_get_clock_offset(packet);
		found[i].named = gap_event_inquiry_result_get_name_available(packet);
		if (found[i].named) {
			int n = gap_event_inquiry_result_get_name_len(packet);
			if (n > (int)sizeof found[i].name - 1) n = sizeof found[i].name - 1;
			memcpy(found[i].name, gap_event_inquiry_result_get_name(packet), n);
			found[i].name[n] = 0;
		} else {
			snprintf(found[i].name, sizeof found[i].name, "(asking its name...)");
		}
		snprintf(status, sizeof status, "Searching... found %d", n_found);
	done:
		break;
	}
	case GAP_EVENT_INQUIRY_COMPLETE:
		if (state == BTKBD_SEARCHING && naming < 0) name_next();
		break;
	case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
		if (naming >= 0) {
			if (hci_event_remote_name_request_complete_get_status(packet) == 0)
				snprintf(found[naming].name, sizeof found[naming].name, "%s",
				         hci_event_remote_name_request_complete_get_remote_name(packet));
			else
				snprintf(found[naming].name, sizeof found[naming].name, "%s", bd_addr_to_str(found[naming].addr));
			found[naming].named = true;
			name_next();
		}
		break;

	/* pairing */
	case HCI_EVENT_PIN_CODE_REQUEST:		/* an older keyboard: type the PIN on it */
		hci_event_pin_code_request_get_bd_addr(packet, addr);
		snprintf(code, sizeof code, "%04u", (unsigned)(get_rand_32() % 10000));
		gap_pin_code_response(addr, code);
		set_status("Type the code on the keyboard");
		break;
	case HCI_EVENT_USER_PASSKEY_NOTIFICATION:
		snprintf(code, sizeof code, "%06u", (unsigned)hci_event_user_passkey_notification_get_numeric_value(packet));
		set_status("Type the code on the keyboard");
		break;
	case HCI_EVENT_USER_CONFIRMATION_REQUEST:	/* "just works" */
		hci_event_user_confirmation_request_get_bd_addr(packet, addr);
		gap_ssp_confirmation_response(addr);
		break;
	case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
		d_pair = packet[2];
		code[0] = 0;
		break;
	case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT:
		d_auth = packet[2];
		code[0] = 0;
		/* paired: remember it now. Keyboards often hang up straight after
		   pairing and then connect back by themselves, and that has to be
		   let in. */
		if (packet[2] == 0 && state == BTKBD_PAIRING && pairing_with >= 0)
			save_paired(found[pairing_with].addr, found[pairing_with].name);
		break;

	case HCI_EVENT_HID_META:
		switch (hci_event_hid_meta_get_subevent_code(packet)) {
		case HID_SUBEVENT_INCOMING_CONNECTION:
			hid_subevent_incoming_connection_get_address(packet, addr);
			if ((paired && bd_addr_cmp(addr, paired_addr) == 0) || state == BTKBD_PAIRING)
				hid_host_accept_connection(hid_subevent_incoming_connection_get_hid_cid(packet), HID_PROTOCOL_MODE_REPORT);
			else
				hid_host_decline_connection(hid_subevent_incoming_connection_get_hid_cid(packet));
			break;
		case HID_SUBEVENT_CONNECTION_OPENED:
			if (hid_subevent_connection_opened_get_status(packet) != ERROR_CODE_SUCCESS) {
				note_err("connect", hid_subevent_connection_opened_get_status(packet));
				cid = 0;
				code[0] = 0;
				if (state == BTKBD_PAIRING) {
					bool ok = pairing_with >= 0 && paired && bd_addr_cmp(paired_addr, found[pairing_with].addr) == 0;
					pairing_with = -1;
					idle_status();
					if (ok) {	/* paired, but not connected yet: it will come back, or we'll try again */
						set_status("Paired: press a key on the keyboard");
						btstack_run_loop_remove_timer(&retry_timer);
						btstack_run_loop_set_timer(&retry_timer, 8000);	/* give it time to call back first */
						btstack_run_loop_add_timer(&retry_timer);
					} else {
						set_status("Pairing failed: try again");
					}
				}
				break;
			}
			cid = hid_subevent_connection_opened_get_hid_cid(packet);
			descriptor_ok = false;
			code[0] = 0;
			if (state == BTKBD_PAIRING && pairing_with >= 0) {
				hid_subevent_connection_opened_get_bd_addr(packet, addr);
				save_paired(addr, found[pairing_with].name);
				pairing_with = -1;
			}
			state = BTKBD_CONNECTED;
			snprintf(status, sizeof status, "Connected: %.30s", paired_name[0] ? paired_name : "keyboard");
			break;
		case HID_SUBEVENT_DESCRIPTOR_AVAILABLE:
			descriptor_ok = hid_subevent_descriptor_available_get_status(packet) == ERROR_CODE_SUCCESS;
			break;
		case HID_SUBEVENT_REPORT:
			if (hid_subevent_report_get_hid_cid(packet) == cid)
				handle_report(hid_subevent_report_get_report(packet), hid_subevent_report_get_report_len(packet));
			break;
		case HID_SUBEVENT_CONNECTION_CLOSED:
			cid = 0;
			descriptor_ok = false;
			release_all();
			idle_status();
			break;
		}
		break;
	}
}

/* ------------------------------------------------------------ the API */

/* BTstack runs in the background (an interrupt); calls into it from the
   program take its lock */
static void lock(void) { async_context_acquire_lock_blocking(cyw43_arch_async_context()); }
static void unlock(void) { async_context_release_lock(cyw43_arch_async_context()); }

bool btkbd_init(void)
{
	if (cyw43_arch_init()) { state = BTKBD_OFF; set_status("Bluetooth didn't start"); return false; }
	lock();
	load_paired();
	l2cap_init();
	hid_host_init(descriptor, sizeof descriptor);
	hid_host_register_packet_handler(packet_handler);
	hci_cb.callback = packet_handler;
	hci_add_event_handler(&hci_cb);
	gap_set_local_name("PicoCalc");
	gap_set_class_of_device(0x00010C);		/* a computer (laptop) */
	gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
	hci_set_master_slave_policy(HCI_ROLE_MASTER);
	gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_ONLY);
	gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_REQUIRED_GENERAL_BONDING);
	gap_set_bondable_mode(1);
	gap_connectable_control(1);			/* so the keyboard can reconnect by itself */
	btstack_run_loop_set_timer_handler(&retry_timer, on_retry);
	btstack_run_loop_set_timer(&retry_timer, RETRY_MS);
	btstack_run_loop_add_timer(&retry_timer);
	state = BTKBD_STARTING;
	set_status("Starting Bluetooth");
	hci_power_control(HCI_POWER_ON);
	unlock();
	return true;
}

void btkbd_poll(void)
{
	/* (Bluetooth itself runs in the background; this only saves a new
	   pairing to the card, which has to be done from the program) */
	if (state == BTKBD_OFF) return;
	if (store_dirty && time_reached(save_after) && state != BTKBD_PAIRING && state != BTKBD_SEARCHING) {
		lock();
		store_save();
		unlock();
	}
}

btkbd_state_t btkbd_state(void) { return state; }
const char *btkbd_status(void) { return status; }
const char *btkbd_name(void) { return paired ? paired_name : ""; }
const char *btkbd_code(void) { return code[0] ? code : NULL; }

void btkbd_search(void)
{
	if (state == BTKBD_OFF || state == BTKBD_STARTING || state == BTKBD_SEARCHING) return;
	lock();
	n_found = 0;
	naming = -1;
	state = BTKBD_SEARCHING;
	set_status("Searching...");
	gap_inquiry_start(8);				/* 8 x 1.28 s */
	unlock();
}

int btkbd_found(void) { return n_found; }
const char *btkbd_found_name(int i) { return i >= 0 && i < n_found ? found[i].name : ""; }

void btkbd_pair(int i)
{
	if (i < 0 || i >= n_found || state == BTKBD_PAIRING) return;
	lock();
	if (state == BTKBD_SEARCHING) {		/* finish the search first, then pair */
		pair_after_search = i;
		snprintf(status, sizeof status, "Stopping the search...");
		if (naming < 0) gap_inquiry_stop();
	} else {
		start_pairing(i);
	}
	unlock();
}

static void start_pairing(int i)
{
	reset_diag();
	if (cid) { hid_host_disconnect(cid); cid = 0; }
	pairing_with = i;
	state = BTKBD_PAIRING;
	snprintf(status, sizeof status, "Pairing with %.28s", found[i].name);
	gap_drop_link_key_for_bd_addr(found[i].addr);	/* start fresh */
	uint8_t err = hid_host_connect(found[i].addr, HID_PROTOCOL_MODE_REPORT, &cid);
	if (err != ERROR_CODE_SUCCESS) {
		note_err("start", err);
		cid = 0;
		pairing_with = -1;
		idle_status();
		set_status("Couldn't connect: try again");
	}
}

void btkbd_forget(void)
{
	lock();
	if (cid) { hid_host_disconnect(cid); cid = 0; }
	if (paired) gap_drop_link_key_for_bd_addr(paired_addr);
	tlv_delete(NULL, TAG_KEYBOARD);
	paired = false;
	paired_name[0] = 0;
	if (state != BTKBD_OFF && state != BTKBD_STARTING) idle_status();
	unlock();
}
